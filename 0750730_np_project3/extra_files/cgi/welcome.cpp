#include <cstdlib>
#include <iostream>
using namespace std;

int main() {
  /* [Required] HTTP Header */
  cout << "Content-type: text/plain" << endl << endl;

  system("figlet Welcome | cowsay -n");
  return 0;
}
