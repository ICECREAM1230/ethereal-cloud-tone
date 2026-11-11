idf_component_register(SRCS "fdc2214.c" "fdc_ranges.c" INCLUDE_DIRS "include")
target_compile_options(${COMPONENT_LIB} PRIVATE -Wall -Wextra -Werror)
