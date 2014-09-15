#bitchttpd
----

`bitchttpd` 是一个简单高效的http server，纯C语言开发，目前主要用在本人个人的网站。

###功能
* 支持文件，目录的访问
* 支持sendfile, tcp_cock等高效操作
* 支持404， 500
* 支持异步的日志：
    * 支持零点自动切换日志文件
    * 完善清晰的格式
    * 多线程支持
* 支持epoll


###待续...
* 急需支持304 not modified


###想法
   可以把图片之类的公用的资源fang在另一个端口
   
   
###粗略的性能测试(700请求，并发500，1G内存，1核cpu)

`毫无压力啊！！`对于本人用来托管博客文章之类的，妥妥的
`后续会有更高性能的测试和大文件的测试`

simon@ubuntu:~/Public/project/bitchttpd$ ab -n 700 -c 500 http://127.0.0.1:6789/file.h
This is ApacheBench, Version 2.3 <$Revision: 655654 $>
Copyright 1996 Adam Twiss, Zeus Technology Ltd, http://www.zeustech.net/
Licensed to The Apache Software Foundation, http://www.apache.org/

Benchmarking 127.0.0.1 (be patient)
Completed 100 requests
Completed 200 requests
Completed 300 requests
Completed 400 requests
Completed 500 requests
Completed 600 requests
Completed 700 requests
Finished 700 requests


Server Software:        bitchttpd/v0.1
Server Hostname:        127.0.0.1
Server Port:            6789

Document Path:          /file.h
Document Length:        291 bytes

Concurrency Level:      500
Time taken for tests:   0.762 seconds
Complete requests:      700
Failed requests:        0
Write errors:           0
Non-2xx responses:      700
Total transferred:      331800 bytes
HTML transferred:       203700 bytes
Requests per second:    918.93 [#/sec] (mean)
Time per request:       544.110 [ms] (mean)
Time per request:       1.088 [ms] (mean, across all concurrent requests)
Transfer rate:          425.36 [Kbytes/sec] received

Connection Times (ms)
              min  mean[+/-sd] median   max
Connect:        0  106  73.6    115     211
Processing:   158  352  87.9    391     459
Waiting:      141  246  58.3    283     316
Total:        237  458 127.8    460     643

Percentage of the requests served within a certain time (ms)
  50%    460
  66%    539
  75%    578
  80%    601
  90%    619
  95%    625
  98%    639
  99%    640
 100%    643 (longest request)
