add_library(communicator_test OBJECT
	./test.cpp
)
target_link_libraries(communicator_test
	PRIVATE
		ramenx
		breeze::module::models
		breeze_module_communicator
		robotpilots::algorithm
)

target_link_libraries(app PRIVATE
    communicator_test
)
