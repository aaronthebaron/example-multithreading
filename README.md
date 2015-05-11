# Example Mutithreading with Threadpools

##threadpooldaemon
An example daemon that fills queues with fibonacci sequence numbers that are then passed to the threads for printing. The point is to show how to build a threadpooling daemon that can wait for input and then rapidly process it. The way the daemon is constructed requires that input cannot be dependant on the results of other threads. I use a similar model for ingesting and processing millions of incoming images from webcams. 

##threadprocess - Not in the repo yet
An "all out" process that is built to manipulate data as quickly as possible, joins the threads, and then terminates. I use something like this for periodically processing a massive amount of information that comes in all at once. Again, the results of a single thread should be discreet, but there are ways to work around this. 

##Make
- You need libpthread
- type 'make'
