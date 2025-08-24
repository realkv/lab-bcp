set(src_dirs 
    "."
    "../../common"
    "../../../../bcp-sdk/src"
    )

set(include_dirs 
    "."
    "../../common"
    "../../../../bcp-sdk/include"
    )

idf_component_register( 
                        SRC_DIRS "${src_dirs}"
                        EXCLUDE_SRCS "${exc_sirs}"
                       INCLUDE_DIRS "${include_dirs}"
                       )

