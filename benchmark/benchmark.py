# Simple automated benchmarking
# T J Atherton Mar 2021-4
# 
# Command line switches
# -f path - Uses an external folder of tests, which are copied in place and modified for use with the bytecodeoptimizer
# -t      - Cleanup copied files

# import necessary modules
import os, glob, sys
import re
import colored
from colored import stylize
import subprocess

"""
Dictionary of languages as keys mapping to corresponding extension
"""
languages = { "morpho6 -O" : "morpho",
              "morpho6" : "morpho", }
samples = 10

prepend = False 

# Gets the output generated
def getoutput(filepath):
    # Load the file
    file_object = open(filepath, 'r')
    lines = file_object.readlines()
    file_object.close()
    # Extract the timing numbers [minutes, seconds]
    times = re.findall(r"[-+]?\d*\.\d+|\d+", lines[0])

    return float(times[0])

    return -1

# Runs a given command with a file and return the time in s.
def run(command, file, param):
    out = -1

    print(command + ' ' + file)
    # Create a temporary file in the same directory
    tmp = file + '.out'

    # Run the test
    exec = '( /usr/bin/time ' + command + ' ' + file + ' ' + param + ' 1> /dev/null ) 2> ' + tmp
    os.system(exec)

    # If we produced output
    if os.path.exists(tmp):
        out=getoutput(tmp)

        # Delete the temporary file
        os.system('rm ' + tmp)

    return out

def findParam(file):
    directory = file if os.path.isdir(file) else os.path.dirname(file)
    in_file_path = os.path.join(directory, 'in.txt')

    if os.path.exists(in_file_path):
        with open(in_file_path, 'r') as f:
            return f.read()

    return ''

def doRun(command, file):
    if (prepend):
        # Prepend the bytecodeoptimizer import
        os.system("sed -i.old '1s;^;import bytecodeoptimizer\\n;' " + file)

    param = findParam(file)

    return run(command, file, param)

# Perform a benchmark
def benchmark(folder):
    dict = {};
    print(stylize(folder[:-1],colored.fg("green")))
    for lang in languages.keys():
        test = glob.glob(folder + '**.' + languages[lang], recursive=False)
        if (len(test)>0):
            time = []
            for i in range(1,samples):
                time.append(doRun(lang, test[0]))
            dict[lang]=min(time)
    return dict

print('--Begin testing---------------------')

success=0 # number of successful tests
total=0   # total number of tests

TIDY=False 
base = ''

for ix, arg in enumerate(sys.argv):
    if arg == '-f':
        base = sys.argv[ix+1]
    if arg == '-t':
        TIDY = True

if (base!=''):
    os.system('mkdir -p tests')
    os.system('cp -r '+base+'/* ./tests/')
    base = 'tests/'
    prepend=True

benchmarks=glob.glob(base + '**/', recursive=True)

out = []

for f in benchmarks:
    times=benchmark(f)
    out.append(times)

# Display output
str="{:<15}".format("")
for lang in languages.keys():
    str+=" "+"{:<8}".format(lang)
print(str)

for i, results in enumerate(out):
    str="{:<15}".format(benchmarks[i][:-1])
    for lang in languages.keys():
        if lang in results:
            str+=" "+"{:<8}".format(results[lang])
        else:
            str+=" "+"{:<8}".format("-")
    print(str)


print('--End testing-----------------------')

# Cleanup the test files 
if TIDY:
    os.system('rm -r tests')