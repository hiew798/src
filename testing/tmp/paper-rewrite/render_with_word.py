"""Use the packaged renderer with Microsoft Word for the Windows PDF stage."""
from pathlib import Path
import importlib.util, os, subprocess, sys

skill = Path(r'C:\Users\cyftl\.codex\plugins\cache\openai-primary-runtime\documents\26.1015.12040\skills\documents')
runtime = Path(r'C:\Users\cyftl\.cache\codex-runtimes\codex-primary-runtime\dependencies')
os.environ['PATH'] = str(runtime/'native/poppler/Library/bin') + os.pathsep + os.environ['PATH']
spec = importlib.util.spec_from_file_location('packaged_render_docx', skill/'render_docx.py')
renderer = importlib.util.module_from_spec(spec)
spec.loader.exec_module(renderer)

def word_pdf(doc_path, user_profile, convert_tmp_dir, stem, verbose):
    output = str(Path(convert_tmp_dir)/(stem+'.pdf'))
    cmd = ['powershell.exe', '-NoProfile', '-ExecutionPolicy', 'Bypass', '-File',
           str(Path(__file__).with_name('export_word_pdf.ps1')),
           '-InputDoc', str(Path(doc_path).resolve()), '-OutputPdf', output]
    result = subprocess.run(cmd, capture_output=True, text=True, timeout=180)
    log = result.stdout + result.stderr
    if verbose: print(log)
    if result.returncode or not Path(output).is_file():
        raise RuntimeError('Microsoft Word export failed: '+log)
    return output, log

renderer.convert_to_pdf = word_pdf
if __name__ == '__main__': renderer.main()
