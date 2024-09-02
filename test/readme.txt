使用virtualbox：
将 Makefile、test、test_close_server_for_server.c 和 close_client_for_server 拷贝到现有 test 目录下
在 /vagrant/tju_tcp/test 路径下，用“test close”测试四次挥手

使用docker：
将 Makefile、test-docker版、test_close_server_for_server.c 和 close_client_for_server 拷贝到现有 test 目录下
将 test-docker版 重命名为 test
在 /vagrant/tju_tcp/test 路径下，用“test close”测试四次挥手