# NVWA for ns-3

NVWA for ns-3 is an ns-3 fork with a datacenter-network module for building
structured topologies and routing policies. The main code lives under
`src/datacenter` and includes Clos/Fat-Tree, Dragonfly, Torus, intra-server
fabrics, ECMP, rule-based routing, failure injection, and non-minimal routing
policies.

## Getting Started

Install the build tools required by the NVWA datacenter module: a
C++23-capable compiler, Python 3, CMake 3.20 or newer, and Ninja.

Ubuntu 22.04+ / Debian 12+:

```bash
sudo apt update
sudo apt install -y build-essential g++ python3 cmake ninja-build
```

Fedora:

```bash
sudo dnf install -y gcc-c++ python3 cmake ninja-build
```

macOS with Homebrew:

```bash
xcode-select --install
brew install cmake ninja
```

Optional ns-3 packages such as GSL and libxml2 are not required for NVWA.

Clone and configure the repository:

```bash
git clone https://github.com/wenkai-tartar/NVWA-for-ns-3.git
cd NVWA-for-ns-3
./ns3 configure -d release --enable-asserts --enable-examples --enable-tests --disable-werror -G Ninja
```

Build the datacenter module and examples:

```bash
./ns3 build datacenter
./ns3 build constructor clos fattree dragonfly torus_detour
```

Build the test runner, then run the focused datacenter test suites:

```bash
./ns3 build test-runner
python3 test.py --no-build --suite structured-address
python3 test.py --no-build --suite ecmp
python3 test.py --no-build --suite ecmp-failure
python3 test.py --no-build --suite intra-server-routing
```

The `--no-build` flag assumes `test-runner` has already been built. If you skip
the `./ns3 build test-runner` step, omit `--no-build` so `test.py` can build the
runner before querying the available suites.

For a full repository check, use:

```bash
./ns3 build
python3 test.py --no-build --verbose-failed
```

## Examples

Datacenter examples are in `src/datacenter/examples`. JSON topology inputs are
in `src/datacenter/examples/inputs`.

For normal use, `constructor` is the unified entry point: write or generate a
JSON topology input, then run it with the desired routing and traffic options.
The topology-specific C++ examples are kept as small reference baselines and
comparison cases; they are not the intended way to add a new topology.

| Example | Source | Purpose | Command |
| --- | --- | --- | --- |
| Constructor | `src/datacenter/examples/constructor.cc` | Recommended unified driver for JSON-defined topologies, routing, failures, and traffic patterns. | `./ns3 run "constructor --config=fattree_k4.json --routing=RuleBased --trafficPattern=flows --numFlows=2 --flowSize=262144"` |
| Clos | `src/datacenter/examples/clos.cc` | Reference C++ baseline for a small Clos topology. | `./ns3 run clos` |
| Fat-Tree | `src/datacenter/examples/fattree.cc` | Reference C++ baseline for a k-ary Fat-Tree topology. | `./ns3 run fattree` |
| Dragonfly | `src/datacenter/examples/dragonfly.cc` | Comparison example for Dragonfly Valiant or UGAL routing on a generated structured topology. | `./ns3 run "dragonfly --groups=5 --routersPerGroup=2 --hostsPerRouter=2 --globalLinksPerRouter=2 --routing=ugal --trafficPattern=flows --numFlows=2 --flowSize=262144"` |
| Torus Detour | `src/datacenter/examples/torus_detour.cc` | Comparison example for non-minimal detour routing on a 3D torus. | `./ns3 run "torus_detour --d=3 --detourStages=2 --transitFields=0,1 --trafficPattern=flows --numFlows=2 --flowSize=262144"` |

Generate JSON topology inputs with:

```bash
python3 src/datacenter/examples/inputs/topology_generator.py fattree --k 4
python3 src/datacenter/examples/inputs/topology_generator.py dragonfly --groups 5 --routers 2 --hosts 2 --global-links 2
python3 src/datacenter/examples/inputs/topology_generator.py rail_optimized --gpus 256 --gpus-per-server 8 --nics-per-aswitch 16 --psw-switches 8
```

The constructor example resolves bare config names such as `fattree_k4.json`
from `src/datacenter/examples/inputs`. Failure files are resolved from
`src/datacenter/examples/inputs/failures` when only a file name is supplied.

Example with a failure injection file:

```bash
./ns3 run "constructor --config=fattree_k4.json --failure=fattree_k4_failure_1_tor_agg.json --routing=RuleBased --trafficPattern=flows --numFlows=2"
```

Use `--help` to list each example's arguments:

```bash
./ns3 run constructor -- --help
./ns3 run dragonfly -- --help
./ns3 run torus_detour -- --help
```

## Module Documentation

The NVWA module is organized as follows:

```text
src/datacenter/
  model/      Core topology, addressing, routing, ECMP, and policy classes.
  helper/     ns-3 helpers for installing routing and topology behavior.
  examples/   Runnable examples and JSON topology inputs.
  test/       Unit and regression tests for the datacenter module.
```

Core topology components:

- `StructuredAddress`, `StructuredAddressDirectory`, and `StructuredTopology`
  define hierarchical node addresses and topology lookup.
- `TopologyBuilder` composes topology levels from reusable templates.
- `ClosInterLevelTemplate`, `FullIntraLevelTemplate`,
  `TorusIntraLevelTemplate`, and `IntraServerLevelTemplate` describe common
  datacenter construction patterns.

Routing components:

- `RuleBasedRouting`, `RoutingRule`, and `RoutingRuleManager` provide structured
  rule-based forwarding with ECMP port selection.
- `NodeBfsRouting` provides graph-search based routing for comparison and
  failure-aware paths.
- `DragonflyValiantRouting`, `DragonflyUgalRouting`, and `TorusDetourRouting`
  implement topology-specific non-minimal routing baselines.
- `ValiantPolicy`, `UgalPolicy`, and `DetourPolicy` can be enabled from JSON via
  the `nonMinimal` configuration block when using `RuleBasedRouting`.

Input configuration:

- Topology JSON files describe `routing`, default `link` parameters, and a list
  of `levels`.
- Each level contains one or more `dims`; each dim selects a template such as
  `ClosInterLevel`, `FullIntraLevel`, `TorusIntraLevel`, or
  `IntraServerLevel`.
- Failure JSON files contain scheduled link failures and are consumed by
  `FailureHelper`.

Tests:

```bash
python3 test.py --no-build --suite structured-address
python3 test.py --no-build --suite ecmp
python3 test.py --no-build --suite ecmp-failure
python3 test.py --no-build --suite intra-server-routing
```

## Links

- NVWA for ns-3 repository: <https://github.com/wenkai-tartar/NVWA-for-ns-3>
- ns-3 project: <https://www.nsnam.org/>
- ns-3 manual: <https://www.nsnam.org/docs/manual/html/>
- ns-3 tutorial: <https://www.nsnam.org/docs/tutorial/html/>
- License: GNU General Public License v2.0, see `LICENSE`.
