#idf_component_register(SRCS "client.cpp"
#                    INCLUDE_DIRS "."
#        )
idf_component_register(SRCS "main.cpp"
        "btstack_impl.cpp" "btstack_impl.h" "sco_demo_util.cpp" "sco_demo_util.h"
        INCLUDE_DIRS ".")
