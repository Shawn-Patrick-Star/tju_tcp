#!/bin/bash

cd .. && cp -r tju_tcp/* /vagrant/tju_tcp/

cd tju_tcp
make clean
make

echo "Build completed successfully."