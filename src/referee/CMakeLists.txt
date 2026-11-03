add_library(referee_test OBJECT
	./test.cpp
)
target_link_libraries(referee_test
	PRIVATE
		ramenx
		breeze::module::models
		breeze_module_referee
		robotpilots::algorithm
)

target_link_libraries(app PRIVATE
    referee_test
)
