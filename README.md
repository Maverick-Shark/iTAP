# iTAP
iTAP, Splits and lists TAP contents, creates idx file (by @Shark)  

Based on STAP by Carmine TSM and ported by [iAN CooG](http://iancoog.altervista.org/).  
Win32/linux portable.  

### Usage:
```
 iTAP <TAP name> [-b] [-l] [-i] [-c] [-n[x]] [-d[x]] [-h[x]] [-k[x]]  
 -b    batch mode, never ask any question  
 -l    list mode, view file list and exit  
 -i    create index file (.idx) with program positions and names  
 -c    create cleaned TAP file (remove small blocks, fix little issues)  
 -n[x] output filenames style. x can be from 0 to 3  
    0: tapname_progressive (default when -n omitted)  
    1: tapname_progressive_filename (equal to -n)  
    2: progressive_filename  
    3: filename  
 -d[x] print debug informations. x is the verboseness, can be from 0 to 2  
    0: no additional info (default when -d omitted)  
    1: info on every header, sync/eof messages (equal to -d)  
    2: debug messages  
 -h[x] Header minimum size (default 7000, try -h5000)  
 -k[x] Block minimum size (default 14000, try -k18000)  
 ```

### Compile
Under Ubuntu:
```
$ gcc itap.c -o itap -w
```
