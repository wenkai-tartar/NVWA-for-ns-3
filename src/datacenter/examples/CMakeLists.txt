set(base_examples
    clos
    fattree
    constructor
    dragonfly
    torus_detour
)

foreach(
  example
  ${base_examples}
)
  build_lib_example(
    NAME ${example}
    SOURCE_FILES ${example}.cc
    LIBRARIES_TO_LINK ${libnetwork}
                      ${libcore}
                      ${libdatacenter}
                      ${libpoint-to-point}
                      ${libnix-vector-routing}
                      ${libinternet}
                      ${libapplications}
  )
endforeach()
