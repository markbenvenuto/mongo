import argparse

def main():
    parser = argparse.ArgumentParser(description='MongoDB IDL Compiler.')

    parser.add_argument(
        '--input',
        type=str,
        help="IDL input file")

    parser.add_argument(
        '--output',
        type=str,
        help="IDL output file prefix")

    parser.add_argument(
        '--include',
        type=str,
        help="Directory to search for IDL import files")
    
    args = parser.parse_args()

    print("Hello")

if __name__ == '__main__':
    main()
