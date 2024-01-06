#idf_component_register(SRCS "client.cpp"
#                    INCLUDE_DIRS "."
#        )
idf_component_register(SRCS "main.cpp" "hf_handle.h" "hf_handle.cpp" "hf_status.h"
        INCLUDE_DIRS ".")
