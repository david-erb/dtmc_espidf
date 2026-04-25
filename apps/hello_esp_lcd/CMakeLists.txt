cmake_minimum_required(VERSION 3.16)

# When any of these sdkconfig files changes you must:
# rm -rf sdkconfig sdkconfig.old build && idf.py set-target esp32 && idf.py reconfigure && idf.py build

include($ENV{IDF_PATH}/tools/cmake/project.cmake)

project(app_esp C)