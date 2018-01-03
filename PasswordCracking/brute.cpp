#include <cstdlib>
#include <fstream>
#include <iostream>
#include <string>

#include "key.hpp"
#include "brute.hpp"
#include "timer.hpp"

std::string me;
std::string encrypted;
std::string table_filename;
bool verbose = false;

Brute::Brute(const std::string& filename) {
	T.resize(N);
	std::string buffer;
    std::fstream input(filename.c_str(), std::ios::in);
    for (int i = 0; i < N; i++) {
        std::getline(input, buffer);
        T[i].set_string(buffer);
    }
    input.close();
}

void Brute::decrypt(const std::string& encrypted) {
  Key passKey(encrypted);
  std::string searchKey ("");
  std::string incKey ("");
  for (int i = 0; i < C - 1; i++) {
    searchKey += "a";
    incKey += "a";
  }
  searchKey += "a";
  incKey += "b";
  Key sKey(searchKey);
  Key iKey(incKey);
  for (int j = 0; j < pow(R, C); j++) {
    if (sKey.subset_sum(T, false) == passKey) {
      sKey.print();
    }
    sKey += iKey;
  }
}

void usage(const std::string& error_msg="") {
	if (!error_msg.empty()) std::cout << "ERROR: " << error_msg << '\n';
	std::cout << me << ": Brute force cracking of Subset-sum password with " 
		<< B << " bits precision\n"
	    << "USAGE: " << me << " <encrypted> <table file> [options]\n"
		<< "\nArguments:\n"
		<< " <encrypted>:   encrypted password to crack\n"
		<< " <table file>:  name of file containing the table to use\n"
		<< "\nOptions:\n"
		<< " -h|--help:     print this message\n"
		<< " -v|--verbose:  select verbose mode\n\n";
	exit(0);
}

void initialize(int argc, char* argv[]) {
	me = argv[0];
	if (argc < 3) usage("Missing arguments");
	encrypted = argv[1];
	table_filename = argv[2];
	for (int i=3; i<argc; ++i) {
		std::string arg = argv[i];
		if (arg == "-h" || arg == "--help") usage();
		else if (arg == "-v" || arg == "--verbose") verbose = true;
		else usage("Unrecognized argument: " + arg);
	}
}


int main(int argc, char *argv[]){
  CPU_timer t;
  initialize(argc, argv);	
	Brute b(argv[2]);
	t.tic();
	b.decrypt(argv[1]);
	t.toc();
	std::cout << "time elapsed" << t.elapsed() << "milliseconds\n";
	return 0;
}
