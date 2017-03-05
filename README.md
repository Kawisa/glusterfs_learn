####Overview  
   工作和glusterfs相关，这里写下一些自己阅读glusterfs源码的一些记录，刚参加工作的小菜鸟，可能有些描述不是很准确。   

* glusterfs的synctask部分，实现了一个多工作线程的线程池，用于执行一些异步任务或者同步任务。见Synctask文件夹
