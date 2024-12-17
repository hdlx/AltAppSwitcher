import utility
import sys
if __name__ == "__main__": 
    args = sys.argv[1:]
    utility.MakeDirs(args[0], args[1])