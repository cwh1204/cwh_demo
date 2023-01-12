# 编写一个程序，解析test文件，并将结果输出到result文件中
要求：<br>
开启三个线程<br>
线程a负责将test文件内容读取到缓冲区A<br>
线程b负责将缓冲区A中的每行内容包含`E CmaX`或者`E CHIUSECASE`关键字的这行内容写入缓冲区B<br>
线程c将缓冲区B写入文件result<br>
缓冲区A和B的大小都为`1024Bytes`<br>