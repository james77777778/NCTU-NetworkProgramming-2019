#! /usr/bin/env python3
import sys
import time
import html

def output_shell(session, content):
    content = html.escape(content)
    content = content.replace('\n', '&NewLine;')
    print(f"<script>document.getElementById('{session}').innerHTML += '{content}';</script>")
    sys.stdout.flush()

def output_command(session, content):
    content = html.escape(content)
    content = content.replace('\n', '&NewLine;')
    print(f"<script>document.getElementById('{session}').innerHTML += '<b>{content}</b>';</script>")
    sys.stdout.flush()

print('Content-type: text/html', end='\r\n\r\n')

print(
'''
<!DOCTYPE html>
<html lang="en">
  <head>
    <meta charset="UTF-8" />
    <title>NP Project 3 Console</title>
    <link
      rel="stylesheet"
      href="https://stackpath.bootstrapcdn.com/bootstrap/4.1.3/css/bootstrap.min.css"
      integrity="sha384-MCw98/SFnGE8fJT3GXwEOngsV7Zt27NXFoaoApmYm81iuXoPkFOJwJ8ERdknLPMO"
      crossorigin="anonymous"
    />
    <link
      href="https://fonts.googleapis.com/css?family=Source+Code+Pro"
      rel="stylesheet"
    />
    <link
      rel="icon"
      type="image/png"
      href="https://cdn0.iconfinder.com/data/icons/small-n-flat/24/678068-terminal-512.png"
    />
    <style>
      * {
        font-family: 'Source Code Pro', monospace;
        font-size: 1rem !important;
      }
      body {
        background-color: #212529;
      }
      pre {
        color: #cccccc;
      }
      b {
        color: #ffffff;
      }
    </style>
  </head>
  <body>
    <table class="table table-dark table-bordered">
      <thead>
        <tr>
          <th scope="col">nplinux7.cs.nctu.edu.tw:3333</th>
          <th scope="col">nplinux8.cs.nctu.edu.tw:4444</th>
          <th scope="col">nplinux9.cs.nctu.edu.tw:5555</th>
        </tr>
      </thead>
      <tbody>
        <tr>
          <td><pre id="s0" class="mb-0"></pre></td>
          <td><pre id="s1" class="mb-0"></pre></td>
          <td><pre id="s2" class="mb-0"></pre></td>
        </tr>
      </tbody>
    </table>
  </body>
</html>
'''
)

output_shell('s0', '% ')
output_command('s0', 'ls\n')
output_shell('s0', 'bin\n')
output_shell('s0', 'test.html\n')
output_shell('s0', '% ')
output_command('s0', 'exit\n')

output_shell('s1', '% ')
output_shell('s2', '% ')
time.sleep(0.5)
output_command('s1', 'ls\n')
time.sleep(0.5)
output_shell('s1', 'bin\n')
output_command('s2', 'ls\n')
time.sleep(0.5)
output_shell('s1', 'test.html\n')
time.sleep(0.5)
output_shell('s1', '% ')
output_shell('s2', 'bin\n')
time.sleep(0.5)
output_command('s1', 'exit\n')
time.sleep(0.5)
output_shell('s2', 'test.html\n')
time.sleep(0.5)
time.sleep(0.5)
output_shell('s2', '% ')
time.sleep(0.5)
time.sleep(0.5)
output_command('s2', 'exit\n')
