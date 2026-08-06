import pycdlib
import sys

def create_iso(input_bin, output_iso):
    iso = pycdlib.PyCdlib()
    iso.new()
    
    iso.add_file(str(input_bin), '/BOOT.BIN;1')
    
    iso.add_eltorito('/BOOT.BIN;1', media_name='noemul', platform_id=0)
    
    iso.write(str(output_iso))
    iso.close()
    print(f"Created {output_iso} successfully!")

if __name__ == '__main__':
    input_file = sys.argv[1] if len(sys.argv) > 1 else 'os-image.bin'
    output_file = sys.argv[2] if len(sys.argv) > 2 else 'useDOS.iso'
    create_iso(input_file, output_file)