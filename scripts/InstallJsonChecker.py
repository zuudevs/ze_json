from pathlib import Path
from sys import argv
from zipfile import ZipFile
from os import getenv
import shutil
import urllib3

PACKAGE_NAME = 'json_checker'
TEMP_DIR = Path(getenv('TEMP', '')).resolve() / PACKAGE_NAME if getenv('TEMP', '') else Path(__file__).resolve().parent / '.temp' / PACKAGE_NAME
BASE_URL = 'https://www.json.org/JSON_checker'

ITEMS = [
    'JSON_checker.h',
    'utf8_to_utf16.h',
    'utf8_decode.h',
    'JSON_checker.c',
    'utf8_to_utf16.c',
    'utf8_decode.c',
    'test.zip'
]

input_argument = {
    'package_dir': '',
    'corpus_dir': '',
}

http = urllib3.PoolManager()

def get_package():
    TEMP_DIR.mkdir(parents=True, exist_ok=True)
    
    for item in ITEMS:
        target_url = f"{BASE_URL}/{item}"
        
        try: 
            res = http.request('GET', target_url)
            
            if res.status == 200:
                target_path = TEMP_DIR / item
                with open(target_path, 'wb') as w:
                    w.write(res.data)
                    
                print(f"[+] Succesfully downloading: {target_url}")
                    
                if target_path.suffix == '.zip':
                    with ZipFile(target_path, 'r') as z:
                        z.extractall(TEMP_DIR)
                    
                    print(f"[+] Successfully extract to: {TEMP_DIR / item.removesuffix('.zip')}")
                    
                    target_path.unlink()
                    print(f"[+] Removing zip: {target_path}")
                    
            else:
                print(f"[-] Failed downloading {target_url} (Status: {res.status})")
                return False
        except Exception as e:
            print(f"[x] Failed processing {item}: {e}")
            return False
    return True

def install():
    c_files = sorted(set(input_argument['package_dir'].glob('*.c')))
    target_name = PACKAGE_NAME.lower()
    
    with open(input_argument['package_dir'] / 'CMakeLists.txt', 'w') as w:
        w.write(f'add_library({target_name}\n')
        for f in c_files:
            w.write(f'\t"${{CMAKE_CURRENT_SOURCE_DIR}}/{f.name}"\n')
        w.write(')\n\n')
        w.write(f'target_include_directories({target_name} PUBLIC\n\t$<BUILD_INTERFACE:${{CMAKE_CURRENT_SOURCE_DIR}}>\n\t$<INSTALL_INTERFACE:include>\n)\n\n')
        
        print(f"[+] Successfully Write CMakeLists.txt into: {input_argument['package_dir'] / 'CMakeLists.txt'}")

def write_info():
    sample_content = "Test suite from http://json.org/JSON_checker/.\n\nIf the JSON_checker is working correctly, it must accept all of the pass*.json files and reject all of the fail*.json files."
    with open(input_argument['corpus_dir'] / 'README.md', 'w') as w:
        w.write(sample_content)
        
    library_content = "# JSON_checker\nJSON_checker is a Pushdown Automaton that very quickly determines if a JSON text is syntactically correct. It could be used to filter inputs to a system, or to verify that the outputs of a system are syntactically correct. It could be adapted to produce a very fast JSON parser.\n\nJSON_checker is made up of these files:\n\n| Filename | Description |\n|---|---|\n| JSON_checker.c | The JSON_checker. |\n| JSON_checker.h | The JSON_checker header file. |\n\n**Accessories**\n\n| Filename | Description |\n|---|---|\n| utf8_to_utf16.c | A UTF-8 to UTF-16 converter. |\n| utf8_to_utf16.h | The UTF-8 to UTF-16 converter header file. |\n| utf8_decode.c | A UTF-8 decoder. |\n| utf8_decode.h | The UTF-8 decoder header file. |\n\nJSON_checker comes with a [test suite](https://www.json.org/JSON_checker/test.zip). If the JSON_checker is working correctly, it must accept all of the `pass*.json` files and reject all of the `fail*.json` files."
    with open(input_argument['package_dir'] / 'README.md', 'w', encoding='utf-8') as w:
        w.write(library_content)
        
def main():
    if len(argv) < 2:
        print('[-] Required output path argument!')
        exit(1)
    elif len(argv) > 5:
        print('[-] Too many argument!')
        exit(1)
    
    cursor = 1
    decision = 0
    if argv[cursor] == '--corpus-dir':
        input_argument['corpus_dir'] = Path(argv[cursor + 1]).resolve()
        decision = 1
    elif argv[cursor] == '--package-dir':
        input_argument['package_dir'] = Path(argv[cursor + 1]).resolve()
        decision = -1
    else:
        print('[-] Invalid Argument!')
        exit(1)
    
    cursor += 2
    
    if decision == 1 and argv[cursor] == '--package-dir':
        input_argument['package_dir'] = Path(argv[cursor + 1]).resolve()
    elif decision == -1 and argv[cursor] == '--corpus-dir':
        input_argument['corpus_dir'] = Path(argv[cursor + 1]).resolve()
    else:
        print('[-] Invalid Argument!')
        exit(1)
  
    if get_package():
        input_argument['package_dir'].mkdir(parents=True, exist_ok=True)
        input_argument['corpus_dir'].mkdir(parents=True, exist_ok=True)
        
        for file_or_dir in TEMP_DIR.iterdir():
            if file_or_dir.is_dir() and file_or_dir.name == 'test':
                for file in file_or_dir.iterdir():                    
                    shutil.move(str(file), str(input_argument['corpus_dir'] / file.name))
                
                file_or_dir.rmdir()
                continue
            
            shutil.move(str(file_or_dir), str(input_argument['package_dir'] / file_or_dir.name))
            
        TEMP_DIR.rmdir()
        print(f"[+] All files moved to: {input_argument['package_dir']}")
        
        write_info()
        install()
        
    exit(0)    

if __name__ == '__main__':
    main()