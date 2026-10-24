from pathlib import Path
import logging
import shutil
import urllib3
import zipfile

TEMP_DIR = Path(__file__).resolve().parent / '.tmp'
SAVED_DIR = Path(__file__).resolve().parents[2] / 'tests/JSON_checker_test'
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

def main():
    TEMP_DIR.mkdir(parents=True, exist_ok=True)
    
    for item in ITEMS:
        target_url = f"{BASE_URL}/{item}"
        
        try: 
            res = http.request('GET', target_url)
            
            if res.status == 200:
                target_path = TEMP_DIR / item
                with open(target_path, 'wb') as w:
                    w.write(res.data)
                    
                logging.info(f"Berhasil mengunduh: {target_url}")
                    
                if target_path.suffix == '.zip':
                    with zipfile.ZipFile(target_path, 'r') as z:
                        z.extractall(TEMP_DIR)
                    
                    logging.info(f"Selesai ekstrak ke: {TEMP_DIR / item.removesuffix('.zip')}")
                    
                    target_path.unlink()
                    logging.info(f"File zip dihapus: {target_path}")
                    
            else:
                logging.warning(f"Gagal mengunduh {target_url} (Status: {res.status})")
        except Exception as e:
            logging.error(f"Gagal memproses {item}: {e}")
    
    SAVED_DIR.mkdir(parents=True, exist_ok=True)
    
    for file_or_dir in TEMP_DIR.iterdir():
        shutil.move(str(file_or_dir), str(SAVED_DIR / file_or_dir.name))
        
    TEMP_DIR.rmdir()
    logging.info(f"Semua file berhasil dipindahkan ke: {SAVED_DIR}")

if __name__ == '__main__':
    main()