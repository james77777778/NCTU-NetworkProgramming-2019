#include <unistd.h>
#include <iostream>
using namespace std;

int main(int, char* const[], char* const envp[]) {
  /* [Required] HTTP Header */
  cout << "Content-type: text/html" << endl << endl;

  char cwd[4096];
  getcwd(cwd, sizeof(cwd));
  cout << "<b>Current Working Directory</b>" << endl;
  cout << "<pre>" << cwd << "</pre>" << endl;

  cout << "<b>Environment Variables</b>" << endl;
  cout << "<pre>" << endl;
  for (int i = 0; envp[i]; ++i)
    cout << envp[i] << endl;
  cout << "</pre>" << endl;
  return 0;
}
