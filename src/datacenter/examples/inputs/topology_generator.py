#!/usr/bin/env python3
"""
Automatically generate ns-3 datacenter topology configuration files.
Supports multiple topology types: Clos, Fat-Tree, Custom, etc.
"""

import json
import argparse
import os
import math
from typing import Dict, List, Any
from pathlib import Path


class TopologyGenerator:
    """Topology generator class"""
    
    def __init__(self):
        self.templates = {
            "clos": self._generate_clos,
            "fattree": self._generate_fattree,
            "dragonfly": self._generate_dragonfly,
            "rail_optimized": self._generate_rail_optimized,
            "custom": self._generate_custom
        }
    
    def generate(self, topology_type: str, **kwargs) -> Dict[str, Any]:
        """Generate topology config"""
        if topology_type not in self.templates:
            raise ValueError(f"Unsupported topology type: {topology_type}")
        
        return self.templates[topology_type](**kwargs)
    
    def _wrap_level_config(self, level_config: Dict[str, Any], load_balance: str = None) -> Dict[str, Any]:
        """Wrap level config with dims array to match the expected format"""
        # Add loadBalance if specified
        if load_balance:
            level_config["loadBalance"] = load_balance
        return {
            "dims": [level_config]
        }
    
    def _generate_clos(self,
                      spine_count: int = 4,
                      leaf_count: int = 8,
                      hosts_per_leaf: int = 2,
                      bandwidth: str = "10Gbps",
                      delay: str = "1us",
                      routing: str = "RuleBased",
                      load_balance: str = None) -> Dict[str, Any]:
        """
        Generate Clos topology config

        Args:
            spine_count: Number of spine switches
            leaf_count: Number of leaf switches
            hosts_per_leaf: Number of hosts per leaf
            bandwidth: Link bandwidth
            delay: Link delay
            routing: Routing type
            load_balance: Load balance policy (kFirst, kRandom, kByHash)
        """
        config = {
            "routing": routing,
            "link": {
                "bandwidth": bandwidth,
                "delay": delay
            },
            "levels": []
        }

        # Host level (leaf nodes)
        config["levels"].append(self._wrap_level_config({
            "template": "ClosInterLevel",
            "nodeNum": hosts_per_leaf,
            "subBlockNum": leaf_count,
            "groupNum": 1
        }, load_balance))

        # Leaf level
        config["levels"].append(self._wrap_level_config({
            "template": "ClosInterLevel",
            "nodeNum": leaf_count,
            "subBlockNum": spine_count,
            "groupNum": 1
        }, load_balance))

        # Spine level
        config["levels"].append(self._wrap_level_config({
            "template": "ClosInterLevel",
            "nodeNum": spine_count,
            "subBlockNum": 1,  # connect to core network
            "groupNum": 1
        }, load_balance))

        return config
    
    def _generate_fattree(self,
                          k: int = 4,
                          bandwidth: str = "10Gbps",
                          delay: str = "1us",
                          routing: str = "RuleBased",
                          load_balance: str = None) -> Dict[str, Any]:
        """
        Generate Fat Tree topology config

        Args:
            k: Fat Tree parameter (k-ary fat tree)
            bandwidth: Link bandwidth
            delay: Link delay
            routing: Routing type
            load_balance: Load balance policy (kFirst, kRandom, kByHash)
        """
        config = {
            "routing": routing,
            "link": {
                "bandwidth": bandwidth,
                "delay": delay
            },
            "levels": []
        }

        # FatTree k-ary: k must be even
        if k % 2 != 0:
            raise ValueError("FatTree parameter k must be even")

        # Level 1: Edge level (leaf level)
        # k/2 hosts per edge switch
        hosts_per_switch = k // 2

        config["levels"].append(self._wrap_level_config({
            "template": "ClosInterLevel",
            "nodeNum": 1,
            "subBlockNum": hosts_per_switch,   # hosts per edge switch
            "groupNum": 1
        }, load_balance))

        # Level 1: Aggregation level (spine level)
        # Each pod has k/2 edge + k/2 aggregation switches
        pod_num = k
        aggs_per_pod = k // 2
        edges_per_pod = k // 2

        config["levels"].append(self._wrap_level_config({
            "template": "ClosInterLevel",
            "nodeNum": aggs_per_pod,        # aggregation switches per pod
            "subBlockNum": edges_per_pod,    # edge switches per pod
            "groupNum": 1
        }, load_balance))

        # Level 2: Core level
        # Core level connects to pods using SingleInterLevel
        core_group_num = k // 2
        core_switches = core_group_num ** 2

        config["levels"].append(self._wrap_level_config({
            "template": "ClosInterLevel",
            "nodeNum": core_switches,         # core switches
            "subBlockNum": k,            # connect to pods
            "groupNum": core_group_num
        }, load_balance))

        return config
    
    def _generate_custom(self, 
                        levels: List[Dict[str, Any]],
                        bandwidth: str = "10Gbps",
                        delay: str = "1us", 
                        routing: str = "RuleBased") -> Dict[str, Any]:
        """
        Generate custom topology config
        
        Args:
            levels: Custom level config list
            bandwidth: Link bandwidth
            delay: Link delay
            routing: Routing type
        """
        # Process levels to ensure they have dims wrapper
        processed_levels = []
        for level in levels:
            if "dims" in level:
                # Already has dims wrapper
                processed_levels.append(level)
            else:
                # Add dims wrapper
                processed_levels.append(self._wrap_level_config(level))
        
        config = {
            "routing": routing,
            "link": {
                "bandwidth": bandwidth,
                "delay": delay
            },
            "levels": processed_levels
        }
        
        return config

    def _generate_dragonfly(self,
                           groups: int = 9,
                           routers_per_group: int = 4,
                           hosts_per_router: int = 2,
                           global_links_per_router: int = 2,
                           global_link_arrangement: str = "Absolute",
                           bandwidth: str = "10Gbps",
                           delay: str = "1us",
                           routing: str = "RuleBased",
                           load_balance: str = None) -> Dict[str, Any]:
        """
        Generate Dragonfly topology config

        Args:
            groups: Number of groups (g)
            routers_per_group: Routers per group (a)
            hosts_per_router: Hosts per router (p)
            global_links_per_router: Global links per router (h)
            bandwidth: Link bandwidth
            delay: Link delay
            routing: Routing type
            load_balance: Load balance policy (kFirst, kRandom, kByHash)
        """
        if groups <= 0 or routers_per_group <= 0 or hosts_per_router <= 0:
            raise ValueError("groups, routers_per_group, and hosts_per_router must be > 0")
        if global_links_per_router <= 0:
            raise ValueError("global_links_per_router must be > 0")
        if global_link_arrangement not in ("Absolute", "SameRank"):
            raise ValueError("global_link_arrangement must be Absolute or SameRank")
        if global_link_arrangement == "Absolute" and groups != routers_per_group * global_links_per_router + 1:
            raise ValueError("Dragonfly Absolute requires groups = routers_per_group * global_links_per_router + 1")

        config = {
            "routing": routing,
            "link": {
                "bandwidth": bandwidth,
                "delay": delay
            },
            "levels": []
        }

        dims: List[Dict[str, Any]] = []

        # Level 1: hosts to routers (Clos inter-level)
        host_dim = {
            "template": "ClosInterLevel",
            "nodeNum": 1,
            "subBlockNum": hosts_per_router,
            "groupNum": 1
        }
        if load_balance:
            host_dim["loadBalance"] = load_balance
        dims.append(host_dim)

        # Level 2: intra-group full mesh (FullIntra, SameRank)
        local_dim = {
            "template": "FullIntraLevel",
            "nodeNum": 0,
            "subBlockNum": routers_per_group,
            "outLinkNum": 1,
            "linkArrangement": "SameRank"
        }
        if load_balance:
            local_dim["loadBalance"] = load_balance
        dims.append(local_dim)

        # Level 3: inter-group global links (FullIntra, Absolute)
        global_dim = {
            "template": "FullIntraLevel",
            "nodeNum": 0,
            "subBlockNum": groups,
            "outLinkNum": global_links_per_router,
            "linkArrangement": global_link_arrangement
        }
        if load_balance:
            global_dim["loadBalance"] = load_balance
        dims.append(global_dim)

        config["levels"].append({"dims": dims})

        return config

    def _generate_rail_optimized(self,
                                 gpu_num: int = 256,
                                 gpu_per_server: int = 8,
                                 nics_per_aswitch: int = 16,
                                 psw_switches: int = 8,
                                 bandwidth: str = "100Gbps",
                                 delay: str = "1us",
                                 nvlink_bandwidth: str = "900Gbps",
                                 nvlink_delay: str = "100ns",
                                 routing: str = "RuleBased",
                                 load_balance: str = None) -> Dict[str, Any]:
        """Generate a rail-optimized GPU topology."""
        if gpu_num <= 0 or gpu_per_server <= 0:
            raise ValueError("gpu_num and gpu_per_server must be > 0")
        if gpu_num % gpu_per_server != 0:
            raise ValueError("gpu_num must be divisible by gpu_per_server")
        if nics_per_aswitch <= 0 or psw_switches <= 0:
            raise ValueError("nics_per_aswitch and psw_switches must be > 0")

        server_num = gpu_num // gpu_per_server
        segment_num = math.ceil(server_num / nics_per_aswitch)
        asw_switches = segment_num * gpu_per_server

        config = {
            "routing": routing,
            "link": {
                "bandwidth": bandwidth,
                "delay": delay
            },
            "levels": []
        }

        config["levels"].append(self._wrap_level_config({
            "template": "IntraServerLevel",
            "serverNum": server_num,
            "endpointsPerServer": gpu_per_server,
            "linkArrangement": "FullMesh",
            "bandwidth": nvlink_bandwidth,
            "delay": nvlink_delay
        }, load_balance))

        config["levels"].append(self._wrap_level_config({
            "template": "ClosInterLevel",
            "nodeNum": asw_switches,
            "subBlockNum": 1,
            "groupNum": 1,
            "linkArrangement": "RailOptimized",
            "endpointsPerServer": gpu_per_server,
            "nicsPerAswitch": nics_per_aswitch,
            "bandwidth": bandwidth,
            "delay": delay
        }, load_balance))

        config["levels"].append(self._wrap_level_config({
            "template": "ClosInterLevel",
            "nodeNum": psw_switches,
            "subBlockNum": 1,
            "groupNum": 1,
            "bandwidth": bandwidth,
            "delay": delay
        }, load_balance))

        return config
    
    def save_config(self, config: Dict[str, Any], output_file: str) -> None:
        """Save config to file"""
        # Ensure output directory exists
        output_path = Path(output_file)
        output_path.parent.mkdir(parents=True, exist_ok=True)
        
        # Save as formatted JSON
        with open(output_file, 'w', encoding='utf-8') as f:
            json.dump(config, f, indent=2, ensure_ascii=False)
        
        print(f"Topology config file saved to: {output_file}")
        print(f"Total node count: {self._calculate_total_nodes(config)}")
    
    def _calculate_total_nodes(self, config: Dict[str, Any]) -> int:
        """Calculate total node count in topology"""
        total = 0
        for level in config.get("levels", []):
            # Each level now has dims array
            for dim in level.get("dims", []):
                if dim.get("template") in ("IntraServerLevel", "RailOptimizedHostLevel"):
                    endpoints = dim.get("endpointsPerServer", dim.get("gpuPerServer", 0))
                    total += dim.get("serverNum", 0) * endpoints
                else:
                    total += dim.get("nodeNum", 0) * dim.get("subBlockNum", 1)
        return total
    
    def generate_filename_with_params(self, topology_type: str, base_name: str, **kwargs) -> str:
        """Generate filename with parameters as suffix"""
        suffix_parts = []
        
        if topology_type == "clos":
            spine = kwargs.get("spine_count", 4)
            leaf = kwargs.get("leaf_count", 8)
            hosts = kwargs.get("hosts_per_leaf", 2)
            suffix_parts.extend([f"s{spine}", f"l{leaf}", f"h{hosts}"])
            
        elif topology_type == "fattree":
            k = kwargs.get("k", 4)
            suffix_parts.append(f"k{k}")
            
        elif topology_type == "custom":
            # For custom topology:
            # - If user specified output name, honor it directly.
            # - Otherwise, try to derive a readable suffix (e.g., torus_2x2), fallback to hash.
            if base_name != "custom.json":
                return base_name
            levels = kwargs.get("levels", [])
            dims = []
            for level in levels:
                if isinstance(level, dict) and "dims" in level:
                    dims.extend(level.get("dims", []))
                else:
                    dims.append(level)
            torus_dims = []
            if dims and all(isinstance(dim, dict) and dim.get("template") == "TorusIntraLevel"
                            for dim in dims):
                for dim in dims:
                    sub = dim.get("subBlockNum")
                    if isinstance(sub, int) and sub > 0:
                        torus_dims.append(str(sub))
                    else:
                        torus_dims = []
                        break
            if torus_dims:
                name_without_ext = base_name.rsplit('.', 1)[0] if '.' in base_name else base_name
                return f"{name_without_ext}_torus_{'x'.join(torus_dims)}.json"

            # Fallback: hash of the custom levels config
            levels_str = json.dumps(levels, sort_keys=True)
            import hashlib
            hash_obj = hashlib.md5(levels_str.encode())
            suffix_parts.append(f"custom_{hash_obj.hexdigest()[:8]}")
        elif topology_type == "dragonfly":
            groups = kwargs.get("groups", 9)
            routers = kwargs.get("routers_per_group", 4)
            hosts = kwargs.get("hosts_per_router", 2)
            global_links = kwargs.get("global_links_per_router", 2)
            suffix_parts.extend([f"g{groups}", f"a{routers}", f"p{hosts}", f"h{global_links}"])
            global_arrangement = kwargs.get("global_link_arrangement", "Absolute")
            if global_arrangement != "Absolute":
                suffix_parts.append(f"ga{global_arrangement.lower()}")
        elif topology_type == "rail_optimized":
            gpu_num = kwargs.get("gpu_num", 256)
            gpu_per_server = kwargs.get("gpu_per_server", 8)
            nics_per_aswitch = kwargs.get("nics_per_aswitch", 16)
            psw_switches = kwargs.get("psw_switches", 8)
            suffix_parts.extend([
                f"g{gpu_num}",
                f"gps{gpu_per_server}",
                f"npa{nics_per_aswitch}",
                f"psw{psw_switches}"
            ])
        
        # Add routing type if not default
        routing = kwargs.get("routing", "RuleBased")
        if routing != "RuleBased":
            suffix_parts.append(f"r{routing.lower()}")
        
        # Add link parameters if not default
        bandwidth = kwargs.get("bandwidth", "10Gbps")
        delay = kwargs.get("delay", "1us")
        if bandwidth != "10Gbps" or delay != "1us":
            # Clean bandwidth and delay for filename
            bw_clean = bandwidth.replace("Gbps", "g").replace("Mbps", "m").replace("bps", "b")
            delay_clean = delay.replace("us", "u").replace("ns", "n").replace("ms", "m")
            suffix_parts.extend([bw_clean, delay_clean])
        
        # Construct final filename
        if suffix_parts:
            suffix = "_".join(suffix_parts)
            name_without_ext = base_name.rsplit('.', 1)[0] if '.' in base_name else base_name
            return f"{name_without_ext}_{suffix}.json"
        else:
            return base_name


def main():
    """Main function"""
    parser = argparse.ArgumentParser(
        description="Automatically generate ns-3 datacenter topology config file",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog="""
Usage examples:
  # Generate Clos topology (auto filename: clos_s4_l8_h2.json)
  python topology_generator.py clos

  # Generate Clos topology with custom parameters (auto filename: clos_s6_l12_h4_40g_500n.json)
  python topology_generator.py clos --spine 6 --leaf 12 --hosts 4 --bandwidth 40Gbps --delay 500ns

  # Generate Fat Tree topology (auto filename: fattree_k6.json)  
  python topology_generator.py fattree --k 4

  # Generate Dragonfly topology (auto filename: dragonfly_g9_a4_p2_h2.json)
  python topology_generator.py dragonfly --groups 9 --routers 4 --hosts 2 --global-links 2

  # Generate Rail-optimized topology (auto filename includes GPU/server/ASW parameters)
  python topology_generator.py rail_optimized --gpus 256 --gpus-per-server 8 --nics-per-aswitch 16 --psw-switches 8

  # Generate Fat Tree topology with non-minimal routing config
  python topology_generator.py fattree --k 4 --nonminimal --nonminimal-algorithm Valiant --nonminimal-metric bytes --nonminimal-transit-fields 0

  # Specify custom base name
  python topology_generator.py clos -o my_clos.json --spine 4 --leaf 8
        """
    )
    
    parser.add_argument(
        "topology_type", 
        choices=["clos", "fattree", "dragonfly", "rail_optimized", "custom"],
        help="Topology type"
    )
    
    parser.add_argument(
        "-o", "--output",
        help="Output file name (if not specified, auto-generated with parameters)"
    )
    
    parser.add_argument(
        "--routing",
        choices=["RuleBased", "Global"],
        default="RuleBased",
        help="Routing type (default: RuleBased)"
    )

    parser.add_argument(
        "--nonminimal",
        action="store_true",
        help="Enable non-minimal routing config in JSON"
    )
    parser.add_argument(
        "--nonminimal-algorithm",
        choices=["Valiant", "UGAL", "Ugal", "Detour", "detour"],
        default=None,
        help="Non-minimal algorithm (Valiant, UGAL, or Detour)"
    )
    parser.add_argument(
        "--nonminimal-metric",
        choices=["bytes", "packets", "none"],
        default=None,
        help="Non-minimal metric (bytes, packets, none)"
    )
    parser.add_argument(
        "--nonminimal-alpha",
        type=float,
        default=None,
        help="UGAL alpha (default: 1.0)"
    )
    parser.add_argument(
        "--nonminimal-detour-penalty",
        type=float,
        default=None,
        help="UGAL detour penalty (default: 1.0)"
    )
    parser.add_argument(
        "--nonminimal-detour-stages",
        type=int,
        default=None,
        help="Detour stages (default: 1)"
    )
    parser.add_argument(
        "--nonminimal-transit-fields",
        type=str,
        default=None,
        help="Comma/space separated transit field indices (e.g. \"0,1\")"
    )
    parser.add_argument(
        "--nonminimal-seed",
        type=int,
        default=None,
        help="Seed for non-minimal transit selection (default: 1)"
    )
    
    parser.add_argument(
        "--bandwidth",
        default="100Gbps",
        help="Link bandwidth (default: 100Gbps)"
    )
    
    parser.add_argument(
        "--delay",
        default="1us",
        help="Link delay (default: 1us)"
    )

    parser.add_argument(
        "--load-balance",
        choices=["kFirst", "kRandom", "kByHash"],
        help="Load balance policy for all levels (kFirst, kRandom, kByHash). If not specified, uses template default."
    )

    # Clos topology parameters
    parser.add_argument(
        "--spine",
        type=int,
        default=4,
        help="Number of spine switches (Clos topology)"
    )
    
    parser.add_argument(
        "--leaf",
        type=int, 
        default=8,
        help="Number of leaf switches (Clos topology)"
    )
    
    parser.add_argument(
        "--hosts",
        type=int,
        default=2,
        help="Number of hosts per leaf/router (Clos/Dragonfly)"
    )
    
    # Fat Tree parameters
    parser.add_argument(
        "--k",
        type=int,
        default=4,
        help="Fat Tree parameter k (k-ary fat tree)"
    )

    # Dragonfly parameters
    parser.add_argument(
        "--groups",
        type=int,
        default=9,
        help="Number of groups (dragonfly)"
    )
    parser.add_argument(
        "--routers",
        type=int,
        default=4,
        help="Routers per group (dragonfly)"
    )
    parser.add_argument(
        "--global-links",
        type=int,
        default=2,
        help="Global links per router (dragonfly)"
    )
    parser.add_argument(
        "--dragonfly-strategy",
        choices=["Absolute", "SameRank"],
        default="Absolute",
        help="Global link arrangement strategy for dragonfly (Absolute or SameRank)"
    )

    # Rail-optimized topology parameters
    parser.add_argument(
        "--gpus",
        type=int,
        default=256,
        help="Total number of GPUs (rail_optimized)"
    )
    parser.add_argument(
        "--gpus-per-server",
        type=int,
        default=8,
        help="GPUs per server (rail_optimized)"
    )
    parser.add_argument(
        "--nics-per-aswitch",
        type=int,
        default=16,
        help="Servers covered by each ASW per rail (rail_optimized)"
    )
    parser.add_argument(
        "--psw-switches",
        type=int,
        default=8,
        help="Number of PSW switches above the rail ASWs (rail_optimized)"
    )
    parser.add_argument(
        "--nvlink-bandwidth",
        default="900Gbps",
        help="Intra-server GPU link bandwidth (rail_optimized)"
    )
    parser.add_argument(
        "--nvlink-delay",
        default="100ns",
        help="Intra-server GPU link delay (rail_optimized)"
    )
    
    # Custom topology parameters
    parser.add_argument(
        "--levels",
        type=str,
        help="Custom level config (JSON string)"
    )
    
    args = parser.parse_args()
    
    # Create generator
    generator = TopologyGenerator()
    
    try:
        # Generate config by type
        if args.topology_type == "clos":
            config = generator.generate(
                "clos",
                spine_count=args.spine,
                leaf_count=args.leaf,
                hosts_per_leaf=args.hosts,
                bandwidth=args.bandwidth,
                delay=args.delay,
                routing=args.routing,
                load_balance=args.load_balance if hasattr(args, 'load_balance') else None
            )

        elif args.topology_type == "fattree":
            config = generator.generate(
                "fattree",
                k=args.k,
                bandwidth=args.bandwidth,
                delay=args.delay,
                routing=args.routing,
                load_balance=args.load_balance if hasattr(args, 'load_balance') else None
            )
            
        elif args.topology_type == "rail_optimized":
            config = generator.generate(
                "rail_optimized",
                gpu_num=args.gpus,
                gpu_per_server=args.gpus_per_server,
                nics_per_aswitch=args.nics_per_aswitch,
                psw_switches=args.psw_switches,
                bandwidth=args.bandwidth,
                delay=args.delay,
                nvlink_bandwidth=args.nvlink_bandwidth,
                nvlink_delay=args.nvlink_delay,
                routing=args.routing,
                load_balance=args.load_balance if hasattr(args, 'load_balance') else None
            )

        elif args.topology_type == "custom":
            if not args.levels:
                parser.error("--levels argument is required (for custom topology type)")
            
            levels = json.loads(args.levels)
            config = generator.generate(
                "custom",
                levels=levels,
                bandwidth=args.bandwidth,
                delay=args.delay,
                routing=args.routing
            )
        elif args.topology_type == "dragonfly":
            config = generator.generate(
                "dragonfly",
                groups=args.groups,
                routers_per_group=args.routers,
                hosts_per_router=args.hosts,
                global_links_per_router=args.global_links,
                global_link_arrangement=args.dragonfly_strategy,
                bandwidth=args.bandwidth,
                delay=args.delay,
                routing=args.routing,
                load_balance=args.load_balance if hasattr(args, 'load_balance') else None
            )
        
        # Generate output filename with parameters
        if args.output:
            # User specified filename, use it as base name
            final_filename = generator.generate_filename_with_params(
                args.topology_type, args.output,
                spine_count=args.spine,
                leaf_count=args.leaf,
                hosts_per_leaf=args.hosts,
                k=args.k,
                groups=args.groups,
                routers_per_group=args.routers,
                hosts_per_router=args.hosts,
                global_links_per_router=args.global_links,
                global_link_arrangement=args.dragonfly_strategy,
                gpu_num=args.gpus,
                gpu_per_server=args.gpus_per_server,
                nics_per_aswitch=args.nics_per_aswitch,
                psw_switches=args.psw_switches,
                bandwidth=args.bandwidth,
                delay=args.delay,
                routing=args.routing,
                levels=json.loads(args.levels) if args.levels else None
            )
        else:
            # Auto-generate filename based on topology type
            base_name = f"{args.topology_type}.json"
            final_filename = generator.generate_filename_with_params(
                args.topology_type, base_name,
                spine_count=args.spine,
                leaf_count=args.leaf,
                hosts_per_leaf=args.hosts,
                k=args.k,
                groups=args.groups,
                routers_per_group=args.routers,
                hosts_per_router=args.hosts,
                global_links_per_router=args.global_links,
                global_link_arrangement=args.dragonfly_strategy,
                gpu_num=args.gpus,
                gpu_per_server=args.gpus_per_server,
                nics_per_aswitch=args.nics_per_aswitch,
                psw_switches=args.psw_switches,
                bandwidth=args.bandwidth,
                delay=args.delay,
                routing=args.routing,
                levels=json.loads(args.levels) if args.levels else None
            )
        
        # Save config
        nonminimal_requested = (
            args.nonminimal
            or args.nonminimal_algorithm is not None
            or args.nonminimal_metric is not None
            or args.nonminimal_transit_fields is not None
            or args.nonminimal_seed is not None
            or args.nonminimal_alpha is not None
            or args.nonminimal_detour_penalty is not None
            or args.nonminimal_detour_stages is not None
        )
        if nonminimal_requested:
            nm = {"enable": True}
            algorithm = args.nonminimal_algorithm or "Valiant"
            nm["algorithm"] = algorithm
            nm["metric"] = args.nonminimal_metric or "bytes"
            if args.nonminimal_seed is not None:
                nm["seed"] = int(args.nonminimal_seed)
            if args.nonminimal_transit_fields:
                fields = [int(v) for v in args.nonminimal_transit_fields.replace(",", " ").split()]
                nm["transitFields"] = fields
            if algorithm in ("UGAL", "Ugal"):
                nm["alpha"] = args.nonminimal_alpha if args.nonminimal_alpha is not None else 1.0
                nm["detourPenalty"] = (
                    args.nonminimal_detour_penalty
                    if args.nonminimal_detour_penalty is not None
                    else 1.0
                )
            if algorithm in ("Detour", "detour", "DET"):
                nm["detourStages"] = (
                    args.nonminimal_detour_stages
                    if args.nonminimal_detour_stages is not None
                    else 1
                )
            config["nonMinimal"] = nm

        output_path = f"src/datacenter/examples/inputs/{final_filename}"
        generator.save_config(config, output_path)
        
        # Print config summary
        print("\n=== Topology Config Summary ===")
        print(f"Type: {args.topology_type}")
        print(f"Routing: {args.routing}")
        print(f"Link: {args.bandwidth}, {args.delay}")
        print(f"Number of levels: {len(config['levels'])}")
        print(f"Output file: {final_filename}")
        
    except Exception as e:
        print(f"Error: {e}")
        return 1
    
    return 0


if __name__ == "__main__":
    exit(main())

# #!/usr/bin/env python3
# """
# Automatically generate ns-3 datacenter topology configuration files.
# Supports multiple topology types: Clos, Fat-Tree, Custom, etc.
# """

# import json
# import argparse
# import os
# from typing import Dict, List, Any
# from pathlib import Path


# class TopologyGenerator:
#     """Topology generator class"""
    
#     def __init__(self):
#         self.templates = {
#             "clos": self._generate_clos,
#             "fattree": self._generate_fattree,
#             "custom": self._generate_custom
#         }
    
#     def generate(self, topology_type: str, **kwargs) -> Dict[str, Any]:
#         """Generate topology config"""
#         if topology_type not in self.templates:
#             raise ValueError(f"Unsupported topology type: {topology_type}")
        
#         return self.templates[topology_type](**kwargs)
    
#     def _generate_clos(self, 
#                       spine_count: int = 4,
#                       leaf_count: int = 8,
#                       hosts_per_leaf: int = 2,
#                       bandwidth: str = "10Gbps",
#                       delay: str = "1us",
#                       routing: str = "RuleBased") -> Dict[str, Any]:
#         """
#         Generate Clos topology config
        
#         Args:
#             spine_count: Number of spine switches
#             leaf_count: Number of leaf switches  
#             hosts_per_leaf: Number of hosts per leaf
#             bandwidth: Link bandwidth
#             delay: Link delay
#             routing: Routing type
#         """
#         config = {
#             "routing": routing,
#             "link": {
#                 "bandwidth": bandwidth,
#                 "delay": delay
#             },
#             "levels": []
#         }
        
#         # Host level (leaf nodes)
#         config["levels"].append({
#             "template": "ClosInterLevel",
#             "nodeNum": hosts_per_leaf,
#             "subBlockNum": leaf_count
#         })
        
#         # Leaf level
#         config["levels"].append({
#             "template": "ClosInterLevel", 
#             "nodeNum": leaf_count,
#             "subBlockNum": spine_count
#         })
        
#         # Spine level
#         config["levels"].append({
#             "template": "ClosInterLevel",
#             "nodeNum": spine_count,
#             "subBlockNum": 1  # connect to core network
#         })
        
#         return config
    
#     def _generate_fattree(self,
#                           k: int = 4,
#                           bandwidth: str = "10Gbps", 
#                           delay: str = "1us",
#                           routing: str = "RuleBased") -> Dict[str, Any]:
#         """
#         Generate Fat Tree topology config
        
#         Args:
#             k: Fat Tree parameter (k-ary fat tree)
#             bandwidth: Link bandwidth
#             delay: Link delay
#             routing: Routing type
#         """
#         config = {
#             "routing": routing,
#             "link": {
#                 "bandwidth": bandwidth,
#                 "delay": delay
#             },
#             "levels": []
#         }
        
#         # FatTree k-ary: k must be even
#         if k % 2 != 0:
#             raise ValueError("FatTree parameter k must be even")
        
#         # Level 1: Edge level (leaf level)
#         # k/2 hosts per edge switch
#         hosts_per_switch = k // 2
        
#         config["levels"].append({
#             "template": "ClosInterLevel",
#             "nodeNum": 1,
#             "subBlockNum": hosts_per_switch   # hosts per edge switch
#         })
        
#         # Level 1: Aggregation level (spine level) 
#         # Each pod has k/2 edge + k/2 aggregation switches
#         pod_num = k
#         aggs_per_pod = k // 2
#         edges_per_pod = k // 2
        
#         config["levels"].append({
#             "template": "ClosInterLevel",
#             "nodeNum": aggs_per_pod,        # aggregation switches per pod
#             "subBlockNum": edges_per_pod    # edge switches per pod
#         })
        
#         # Level 2: Core level
#         # Core level connects to pods using SingleInterLevel
#         core_group_num = k // 2
#         core_switches = core_group_num ** 2
        
#         config["levels"].append({
#             "template": "SingleInterLevel",
#             "nodeNum": core_switches,         # core switches
#             "subBlockNum": k,            # connect to pods
#             "groupNum": core_group_num
#         })
        
#         return config
    
#     def _generate_custom(self, 
#                         levels: List[Dict[str, Any]],
#                         bandwidth: str = "10Gbps",
#                         delay: str = "1us", 
#                         routing: str = "RuleBased") -> Dict[str, Any]:
#         """
#         Generate custom topology config
        
#         Args:
#             levels: Custom level config list
#             bandwidth: Link bandwidth
#             delay: Link delay
#             routing: Routing type
#         """
#         config = {
#             "routing": routing,
#             "link": {
#                 "bandwidth": bandwidth,
#                 "delay": delay
#             },
#             "levels": levels
#         }
        
#         return config
    
#     def save_config(self, config: Dict[str, Any], output_file: str) -> None:
#         """Save config to file"""
#         # Ensure output directory exists
#         output_path = Path(output_file)
#         output_path.parent.mkdir(parents=True, exist_ok=True)
        
#         # Save as formatted JSON
#         with open(output_file, 'w', encoding='utf-8') as f:
#             json.dump(config, f, indent=2, ensure_ascii=False)
        
#         print(f"Topology config file saved to: {output_file}")
#         print(f"Total node count: {self._calculate_total_nodes(config)}")
    
#     def _calculate_total_nodes(self, config: Dict[str, Any]) -> int:
#         """Calculate total node count in topology"""
#         total = 0
#         for level in config.get("levels", []):
#             total += level.get("nodeNum", 0) * level.get("subBlockNum", 1)
#         return total
    
#     def generate_filename_with_params(self, topology_type: str, base_name: str, **kwargs) -> str:
#         """Generate filename with parameters as suffix"""
#         suffix_parts = []
        
#         if topology_type == "clos":
#             spine = kwargs.get("spine_count", 4)
#             leaf = kwargs.get("leaf_count", 8)
#             hosts = kwargs.get("hosts_per_leaf", 2)
#             suffix_parts.extend([f"s{spine}", f"l{leaf}", f"h{hosts}"])
            
#         elif topology_type == "fattree":
#             k = kwargs.get("k", 4)
#             suffix_parts.append(f"k{k}")
            
#         elif topology_type == "custom":
#             # For custom topology, use a hash of the levels config
#             levels = kwargs.get("levels", [])
#             levels_str = json.dumps(levels, sort_keys=True)
#             import hashlib
#             hash_obj = hashlib.md5(levels_str.encode())
#             suffix_parts.append(f"custom_{hash_obj.hexdigest()[:8]}")
        
#         # Add routing type if not default
#         routing = kwargs.get("routing", "RuleBased")
#         if routing != "RuleBased":
#             suffix_parts.append(f"r{routing.lower()}")
        
#         # Add link parameters if not default
#         bandwidth = kwargs.get("bandwidth", "10Gbps")
#         delay = kwargs.get("delay", "1us")
#         if bandwidth != "10Gbps" or delay != "1us":
#             # Clean bandwidth and delay for filename
#             bw_clean = bandwidth.replace("Gbps", "g").replace("Mbps", "m").replace("bps", "b")
#             delay_clean = delay.replace("us", "u").replace("ns", "n").replace("ms", "m")
#             suffix_parts.extend([bw_clean, delay_clean])
        
#         # Construct final filename
#         if suffix_parts:
#             suffix = "_".join(suffix_parts)
#             name_without_ext = base_name.rsplit('.', 1)[0] if '.' in base_name else base_name
#             return f"{name_without_ext}_{suffix}.json"
#         else:
#             return base_name


# def main():
#     """Main function"""
#     parser = argparse.ArgumentParser(
#         description="Automatically generate ns-3 datacenter topology config file",
#         formatter_class=argparse.RawDescriptionHelpFormatter,
#         epilog="""
# Usage examples:
#   # Generate Clos topology (auto filename: clos_s4_l8_h2.json)
#   python topology_generator.py clos

#   # Generate Clos topology with custom parameters (auto filename: clos_s6_l12_h4_40g_500n.json)
#   python topology_generator.py clos --spine 6 --leaf 12 --hosts 4 --bandwidth 40Gbps --delay 500ns

#   # Generate Fat Tree topology (auto filename: fattree_k6.json)  
#   python topology_generator.py fattree --k 4

#   # Specify custom base name
#   python topology_generator.py clos -o my_clos.json --spine 4 --leaf 8
#         """
#     )
    
#     parser.add_argument(
#         "topology_type", 
#         choices=["clos", "fattree", "custom"],
#         help="Topology type"
#     )
    
#     parser.add_argument(
#         "-o", "--output",
#         help="Output file name (if not specified, auto-generated with parameters)"
#     )
    
#     parser.add_argument(
#         "--routing",
#         choices=["RuleBased", "Global"],
#         default="RuleBased",
#         help="Routing type (default: RuleBased)"
#     )
    
#     parser.add_argument(
#         "--bandwidth",
#         default="10Gbps",
#         help="Link bandwidth (default: 10Gbps)"
#     )
    
#     parser.add_argument(
#         "--delay", 
#         default="1us",
#         help="Link delay (default: 1us)"
#     )
    
#     # Clos topology parameters
#     parser.add_argument(
#         "--spine",
#         type=int,
#         default=4,
#         help="Number of spine switches (Clos topology)"
#     )
    
#     parser.add_argument(
#         "--leaf",
#         type=int, 
#         default=8,
#         help="Number of leaf switches (Clos topology)"
#     )
    
#     parser.add_argument(
#         "--hosts",
#         type=int,
#         default=2,
#         help="Number of hosts per leaf (Clos topology)"
#     )
    
#     # Fat Tree parameters
#     parser.add_argument(
#         "--k",
#         type=int,
#         default=4,
#         help="Fat Tree parameter k (k-ary fat tree)"
#     )
    
#     # Custom topology parameters
#     parser.add_argument(
#         "--levels",
#         type=str,
#         help="Custom level config (JSON string)"
#     )
    
#     args = parser.parse_args()
    
#     # Create generator
#     generator = TopologyGenerator()
    
#     try:
#         # Generate config by type
#         if args.topology_type == "clos":
#             config = generator.generate(
#                 "clos",
#                 spine_count=args.spine,
#                 leaf_count=args.leaf,
#                 hosts_per_leaf=args.hosts,
#                 bandwidth=args.bandwidth,
#                 delay=args.delay,
#                 routing=args.routing
#             )
            
#         elif args.topology_type == "fattree":
#             config = generator.generate(
#                 "fattree",
#                 k=args.k,
#                 bandwidth=args.bandwidth,
#                 delay=args.delay,
#                 routing=args.routing
#             )
            
#         elif args.topology_type == "custom":
#             if not args.levels:
#                 parser.error("--levels argument is required (for custom topology type)")
            
#             levels = json.loads(args.levels)
#             config = generator.generate(
#                 "custom",
#                 levels=levels,
#                 bandwidth=args.bandwidth,
#                 delay=args.delay,
#                 routing=args.routing
#             )
        
#         # Generate output filename with parameters
#         if args.output:
#             # User specified filename, use it as base name
#             final_filename = generator.generate_filename_with_params(
#                 args.topology_type, args.output,
#                 spine_count=args.spine,
#                 leaf_count=args.leaf,
#                 hosts_per_leaf=args.hosts,
#                 k=args.k,
#                 bandwidth=args.bandwidth,
#                 delay=args.delay,
#                 routing=args.routing,
#                 levels=json.loads(args.levels) if args.levels else None
#             )
#         else:
#             # Auto-generate filename based on topology type
#             base_name = f"{args.topology_type}.json"
#             final_filename = generator.generate_filename_with_params(
#                 args.topology_type, base_name,
#                 spine_count=args.spine,
#                 leaf_count=args.leaf,
#                 hosts_per_leaf=args.hosts,
#                 k=args.k,
#                 bandwidth=args.bandwidth,
#                 delay=args.delay,
#                 routing=args.routing,
#                 levels=json.loads(args.levels) if args.levels else None
#             )
        
#         # Save config
#         output_path = f"src/datacenter/examples/inputs/{final_filename}"
#         generator.save_config(config, output_path)
        
#         # Print config summary
#         print("\n=== Topology Config Summary ===")
#         print(f"Type: {args.topology_type}")
#         print(f"Routing: {args.routing}")
#         print(f"Link: {args.bandwidth}, {args.delay}")
#         print(f"Number of levels: {len(config['levels'])}")
#         print(f"Output file: {final_filename}")
        
#     except Exception as e:
#         print(f"Error: {e}")
#         return 1
    
#     return 0


# if __name__ == "__main__":
#     exit(main())
