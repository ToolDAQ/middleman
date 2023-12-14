#!/bin/bash
Dependencies=/opt

# add Dependencies from ToolFramework
export LD_LIBRARY_PATH=${Dependencies}/zeromq-4.3.5/lib:${Dependencies}/boost_1_66_0/install/lib:${Dependencies}/libpqxx-7.8.1/install/lib:$LD_LIBRARY_PATH

