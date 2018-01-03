#include <cstdlib>
#include <fstream>
#include <iostream>
#include <string>
#include <map>

#include "key.hpp"
#include "symbol.hpp"
#include "timer.hpp"

std::string me;
std::string encrypted;
std::string table_filename;
std::map<Key, std::string> m;
bool verbose = false;

Symbol::Symbol(const std::string& filename) {
	T.resize(N);
	std::string buffer;
    std::fstream input(filename.c_str(), std::ios::in);
    for (int i = 0; i < N; i++) {
        std::getline(input, buffer);
        T[i].set_string(buffer);
    }
    input.close();
}

void Symbol::decrypt(const std::string& encrypted){
  char searchKey[C];
  char searchKeyTwo[C];
  char incKey[C];
  char incKeyTwo[C];
  for (int i = 0; i < C; i++) {
    searchKey[i] = 'a';
    searchKeyTwo[i] = 'a';
    incKey[i] = 'a';
    incKeyTwo[i] = 'a';
  }
  incKey[C - 1] = 'b';
  incKeyTwo[C/2 - 1] = 'b';
  Key sKey(searchKey);
  Key iKey(incKey);
  Key sKeyTwo(searchKeyTwo);
  Key iKeyTwo(incKeyTwo);
  for (int j = 0; j < pow(R, C/2); j++) {
      Key esKey = sKey.subset_sum(T, false);
      m.insert({esKey, sKey.toString().substr(C/2, C - 1)});
      sKey += iKey;
  }
  int var = pow(R, C/2);
  if (C % 2 == 1) {
    var = pow(R, (C/2 + 1));
  }
  Key passKey(encrypted);
  for (int k = 0; k < var; k++) {
    Key temp = passKey;
    Key esKeyTwo = sKeyTwo.subset_sum(T, false);
    temp -= esKeyTwo;
    if (m.find(temp) != m.end()) {
      std::string found = m.at(temp);
      Key final(found);
      final += sKeyTwo;
      final.print();
    }
    sKeyTwo += iKeyTwo;
  }
}

void usage(const std::string& error_msg="") {
	if (!error_msg.empty()) std::cout << "ERROR: " << error_msg << '\n';
	std::cout << me << ": Symbol table-based cracking of Subset-sum password"
		<< " with " << B << " bits precision\n"
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
	Symbol s(argv[2]);
	t.tic();
	s.decrypt(argv[1]);
	t.toc();
	  std::cout << "runtime " << t.elapsed() << '\n';
	return 0;
}
