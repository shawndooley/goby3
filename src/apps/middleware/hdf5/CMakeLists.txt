add_executable(goby_hdf5
  hdf5_app.cpp)


target_link_libraries(goby_hdf5
  ${HDF5_LIBRARIES}
  goby
)
