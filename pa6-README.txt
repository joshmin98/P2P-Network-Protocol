Documentation for Programming Assignment 6
==========================================

+------------------------+
| BUILD & RUN (Required) |
+------------------------+

Replace "(Comments?)" with either nunki.usc.edu, VirtualBox with Ubuntu 16.04, or VagrantBox with Ubuntu 16.04:
    The grader should grade my submission on: VirtualBox with Ubuntu 16.04

If grading needs to be done on Ubuntu, replace "(Comments?)" with the names of the
packages your program depends on (to be used in "sudo apt-get install PACKAGENAME"):
    I need the following packages to be installed: libssl-dev

Replace "(Comments?)" with the command the grader should use to compile your code (could simply be "make" or "gmake").
Please understand that the ONLY acceptable compiler is g++ and each compile command must begin with "g++ -g -Wall -std=c++11".
If you ask the grader to use something else, the grader will not be allowed to comply and you will get a zero for your assignment.
    To compile your code, the grader should type: make

This is a PA6-specific question.  Replace "(Comments?)" below with either "yes" or "no.
    I took the "hard state" approach to implement the link state algorithm: (Comments?)

+-------------------------+
| SELF-GRADING (Required) |
+-------------------------+

Replace each "?" below with a numeric value:

(A) neighbors, 2 nodes : 7 out of 7 points
(B) neighbors, 4 nodes : 18 out of 18 points
(C) netgraph, 3 nodes : 22 out of 22 points
(D) netgraph, 4 nodes : 28 out of 28 points
(E) traceroute, 4 nodes : 13 out of 13 points
(F) traceroute, 6 nodes : 12 out of 12 points

Missing/incomplete required section(s) in README file : -? pts
Submitted binary file : -0 pts
Cannot compile : -0 pts
"make clean" : -0 pts
Segmentation faults : -0 pts

+---------------------------------+
| BUGS / TESTS TO SKIP (Required) |
+---------------------------------+

Are there are any tests mentioned in the grading guidelines test suite that you
know that it's not working and you don't want the grader to run it at all so you
won't get extra deductions, please replace "(Comments?)" below with your list.
(Of course, if the grader won't run such tests in the plus points section, you
will not get plus points for them; if the garder won't run such tests in the
minus points section, you will lose all the points there.)  If there's nothing
the grader should skip, please replace "(Comments?)" with "none".

Please skip the following tests: none

+------------------------------------------------------------------------------------------+
| ADDITIONAL INFORMATION FOR GRADER (Optional, but the grader will read what you add here) |
+------------------------------------------------------------------------------------------+
I generate more link-state messages than the reference (grading guidelines). My program still functions up to spec, and ***the correct LSUPDATE message log messages are in the logfile***, however the output can be excessive.

Because of this, it took me a while to self-grade, but after parsing through my logs (which took a... reasonable amount of time) I found that I was passing for all of the test cases. I'm not sure how grading is done on your end (maybe grepping/diffing the logs?), but my apologies for the lengthy output.
+-----------------------------------------------+
| OTHER (Optional) - Not considered for grading |
+-----------------------------------------------+

Comments on design decisions: (Comments?)
