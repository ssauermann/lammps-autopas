#include "autopas.h"
#include "atom.h"
#include "domain.h"
#include "error.h"
#include "memory.h"
#include "neighbor.h"

using namespace LAMMPS_NS;

AutoPasLMP::AutoPasLMP(class LAMMPS *lmp, int narg, char **args) : Pointers(
    lmp) {
  autopas_exists = 1;
  lmp->autopas = this;
}

AutoPasLMP::~AutoPasLMP() = default;

void AutoPasLMP::init_autopas(double cutoff, double **epsilon, double **sigma) {

  _autopas = std::make_unique<AutoPasType>();

  // Initialize particle properties
  _particlePropertiesLibrary = std::make_unique<ParticlePropertiesLibraryType>(
      cutoff);

  for (int i = 1; i <= atom->ntypes; ++i) {
    std::cout << "Type, Eps, Sig: " << i << " " << epsilon[i][i] << " "
              << sigma[i][i] << "\n";
    _particlePropertiesLibrary->addType(
        i, epsilon[i][i], sigma[i][i], atom->mass[i]
    );
  }

  // _autopas.setAllowedCellSizeFactors(*cellSizeFactors);
  auto sensibleContainerOptions = autopas::ContainerOption::getAllOptions();
  sensibleContainerOptions.erase(
      autopas::ContainerOption::directSum); // Never good choice
  _autopas->setAllowedContainers(sensibleContainerOptions);

  auto sensibleTraversalOptions = autopas::TraversalOption::getAllOptions();
  sensibleTraversalOptions.erase(
      autopas::TraversalOption::verletClusters); //  Segfault
  sensibleTraversalOptions.erase(
      autopas::TraversalOption::verletClustersColoring); // Segfault
  sensibleTraversalOptions.erase(
      autopas::TraversalOption::verletClustersStatic); // Segfault
  sensibleTraversalOptions.erase(
      autopas::TraversalOption::verletClusterCells); // Segfault
  _autopas->setAllowedTraversals(sensibleTraversalOptions);

  _autopas->setAllowedContainers({autopas::ContainerOption::linkedCells});
  _autopas->setAllowedDataLayouts({autopas::DataLayoutOption::soa});
  _autopas->setAllowedNewton3Options({autopas::Newton3Option::disabled}); //TODO Newton based on lammps settings
  _autopas->setAllowedTraversals({autopas::TraversalOption::c04});

  FloatVecType boxMax{}, boxMin{};
  std::copy(std::begin(domain->boxhi), std::end(domain->boxhi), boxMax.begin());
  std::copy(std::begin(domain->boxlo), std::end(domain->boxlo), boxMin.begin());
  _autopas->setBoxMax(boxMax);
  _autopas->setBoxMin(boxMin);

  _autopas->setCutoff(
      cutoff); // TODO_AUTOPAS Test: cut_global (PairLJCut) or cutforce (Pair)
  //_autopas.setNumSamples(tuningSamples);
  //_autopas.setSelectorStrategy(selectorStrategy);
  _autopas->setTuningInterval(1000);
  _autopas->setTuningStrategyOption(autopas::TuningStrategyOption::fullSearch);

  neighbor->every = 1; //TODO AutoPas cant handle adding particles that are out of bounds
  //_autopas.setVerletClusterSize(_config->verletClusterSize);
  _autopas->setVerletRebuildFrequency(neighbor->every);
  std::cout << neighbor->skin << "\n";
  // _autopas.setVerletSkin(neighbor->skin);
  _autopas->setVerletSkin(0); // No skin needed when rebuilding every step

  autopas::Logger::create();
  autopas::Logger::get()->set_level(autopas::Logger::LogLevel::warn);

  _autopas->init();

  // Handle particles that got added before AutoPas was initialized
  int nlocal = atom->nlocal;
  for (int i = 0; i < nlocal; i++) {

    FloatVecType pos{atom->x[i][0], atom->x[i][1], atom->x[i][2]};
    FloatVecType vel{atom->v[i][0], atom->v[i][1], atom->v[i][2]};
    unsigned long moleculeId = i;
    unsigned long typeId = atom->type[i];

    _autopas->addParticle(ParticleType(pos, vel, moleculeId, typeId));
  }

  memory->destroy(atom->x);
  memory->destroy(atom->v);
  // TODO When to copy back?
}

#pragma clang diagnostic push
#pragma ide diagnostic ignored "UnreachableCode" // constexpr-if makes function unreachable?
template<bool owned, bool halo>
AutoPasLMP::ParticleType *AutoPasLMP::particle_by_index(int idx) {
  // TODO Global to local mapping?

  auto iteratorBehavior = autopas::IteratorBehavior::ownedOnly;
  if constexpr (halo) {
    iteratorBehavior = autopas::IteratorBehavior::haloOnly;
    if constexpr (owned) {
      iteratorBehavior = autopas::IteratorBehavior::haloAndOwned;
    }
  }

  ParticleType *particle = nullptr;

  // Assuming the particle ids are unique, no reduction is necessary
#pragma omp parallel default(none) shared(idx, particle, iteratorBehavior)
  for (auto iter = _autopas->begin(iteratorBehavior); iter.isValid(); ++iter) {
    auto &p = *iter;
    if (this->idx(p) == idx) {
      particle = &p;
    }
  }

  return particle;

}
#pragma clang diagnostic pop

unsigned long AutoPasLMP::idx(const AutoPasLMP::ParticleType &p) {
  return p.getID(); // TODO Global to local particle mapping // TODO Halo particles?
}

void AutoPasLMP::update_autopas() {
  auto&&[invalidParticles, updated] = _autopas->updateContainer();
  _leavingParticles = std::move(invalidParticles);
}

void AutoPasLMP::copy_back() {
  auto nmax = _autopas->getNumberOfParticles();
  atom->x = memory->grow(atom->x,nmax,3,"atom:x");
  atom->v = memory->grow(atom->v,nmax,3,"atom:v");
  atom->f = memory->grow(atom->f,nmax,3,"atom:f");
  // auto &f = memory->grow(atom->f,nmax*comm->nthreads,3,"atom:f");

#pragma omp parallel default(none)
  for (auto iter = _autopas->begin(autopas::ownedOnly); iter.isValid(); ++iter) {
    auto &p = *iter;
    auto idx = this->idx(p);
    std::copy_n(p.getR().begin(), 3, atom->x[idx]);
    std::copy_n(p.getV().begin(), 3, atom->v[idx]);
    std::copy_n(p.getF().begin(), 3, atom->f[idx]);
  }
}


template AutoPasLMP::ParticleType *AutoPasLMP::particle_by_index<true, true>(int idx);
template AutoPasLMP::ParticleType *AutoPasLMP::particle_by_index<true, false>(int idx);
template AutoPasLMP::ParticleType *AutoPasLMP::particle_by_index<false, true>(int idx);
