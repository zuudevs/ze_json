from pathlib import Path
import logging
import shutil
import urllib3
import zipfile

TEMP_DIR = Path(__file__).resolve().parent / '.tmp'
PACKAGE_NAME = 'JSON_checker'
PACKAGE_DIR = Path(__file__).resolve().parents[2] / 'third_party' / PACKAGE_NAME
SAMPLE_DIR = Path(__file__).resolve().parents[2] / 'samples' / PACKAGE_NAME
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

http = urllib3.PoolManager()
logging.basicConfig(level=logging.INFO, format="%(levelname)s: %(message)s")

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
                    
                logging.info(f"Succesfully downloading: {target_url}")
                    
                if target_path.suffix == '.zip':
                    with zipfile.ZipFile(target_path, 'r') as z:
                        z.extractall(TEMP_DIR)
                    
                    logging.info(f"Successfully extract to: {TEMP_DIR / item.removesuffix('.zip')}")
                    
                    target_path.unlink()
                    logging.info(f"Removing zip: {target_path}")
                    
            else:
                logging.warning(f"Failed downloading {target_url} (Status: {res.status})")
                return False
        except Exception as e:
            logging.error(f"Failed processing {item}: {e}")
            return False
    return True

def install():
    c_files = sorted(set(PACKAGE_DIR.glob('*.c')))
    target_name = PACKAGE_NAME.lower()
    
    with open(PACKAGE_DIR / 'CMakeLists.txt', 'w') as w:
        w.write(f'add_library({target_name}\n')
        for f in c_files:
            w.write(f'\t"${{CMAKE_CURRENT_SOURCE_DIR}}/{f.name}"\n')
        w.write(')\n\n')
        
        w.write(f'target_include_directories({target_name} PUBLIC\n\t$<BUILD_INTERFACE:${{CMAKE_CURRENT_SOURCE_DIR}}>\n\t$<INSTALL_INTERFACE:include>\n)\n\n')
        
        w.write(f'target_compile_definitions({target_name} PUBLIC\n\tSAMPLE_DIR="${{CMAKE_SOURCE_DIR}}/samples/JSON_Checker"\n)\n')
        
        logging.info(f"Successfully Write CMakeLists.txt into: {PACKAGE_DIR / 'CMakeLists.txt'}")

def write_info():
    sample_content = "Test suite from http://json.org/JSON_checker/.\n\nIf the JSON_checker is working correctly, it must accept all of the pass*.json files and reject all of the fail*.json files."
    with open(SAMPLE_DIR / 'README.md', 'w') as w:
        w.write(sample_content)
        
    library_content = "![](JSON_checker/json160.gif)\n# JSON_checker\nJSON_checker is a Pushdown Automaton that very quickly determines if a JSON text is syntactically correct. It could be used to filter inputs to a system, or to verify that the outputs of a system are syntactically correct. It could be adapted to produce a very fast JSON parser.\n\nJSON_checker is made up of these files:\n\n| Filename | Description |\n|---|---|\n| JSON_checker.c | The JSON_checker. |\n| JSON_checker.h | The JSON_checker header file. |\n\n**Accessories**\n\n| Filename | Description |\n|---|---|\n| utf8_to_utf16.c | A UTF-8 to UTF-16 converter. |\n| utf8_to_utf16.h | The UTF-8 to UTF-16 converter header file. |\n| utf8_decode.c | A UTF-8 decoder. |\n| utf8_decode.h | The UTF-8 decoder header file. |\n\nJSON_checker comes with a [test suite](https://www.json.org/JSON_checker/test.zip). If the JSON_checker is working correctly, it must accept all of the `pass*.json` files and reject all of the `fail*.json` files."
    with open(PACKAGE_DIR / 'README.md', 'w', encoding='utf-8') as w:
        w.write(library_content)
        
def main():
    if get_package():
        PACKAGE_DIR.mkdir(parents=True, exist_ok=True)
        SAMPLE_DIR.mkdir(parents=True, exist_ok=True)
        
        for file_or_dir in TEMP_DIR.iterdir():
            if file_or_dir.is_dir() and file_or_dir.name == 'test':
                for file in file_or_dir.iterdir():                    
                    shutil.move(str(file), str(SAMPLE_DIR / file.name))
                
                file_or_dir.rmdir()
                continue
            
            shutil.move(str(file_or_dir), str(PACKAGE_DIR / file_or_dir.name))
            
        TEMP_DIR.rmdir()
        logging.info(f"All files moved to: {PACKAGE_DIR}")
        
        write_info()
        install()        

if __name__ == '__main__':
    main()