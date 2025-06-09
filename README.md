This test reproduces the problem with wrong values of huge page counters in linux kernel on NUMA systems.

Building the test:

gcc hugetlb_test.c -o hugetlb_test

The test takes 2 arguments from cmdline:
argv[1] - initial number of huge pages in system to allocate
argv[2] - maximum number of huge pages, which is allowed to be allocated (including overcommit).
Test mmaps argv[2] number of huge pages, touches each page and unmaps all pages page-by-page.
After each step test checks huge page counters in system and prints error message if values
are wrong. The larger number of huge pages involved, the higher the probability of the problem
reproducing.

Examples of test run:

./huge_page_test 100 120

./huge_page_test 500 1200
