set(component_srcs "src/JkkSchedule.c"
                   "src/JkkScheduleNvs.c")

idf_component_register(SRCS "${component_srcs}"
                       INCLUDE_DIRS "include"
                       PRIV_INCLUDE_DIRS "src"
                       REQUIRES "nvs_flash")

set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wno-unused-function")
