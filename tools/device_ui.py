"""Small ADB UI test helper; clicks only an exact visible text match."""
import argparse,re,subprocess,time,xml.etree.ElementTree as ET
p=argparse.ArgumentParser();p.add_argument('action',choices=['list','click']);p.add_argument('text',nargs='?');args=p.parse_args()
subprocess.run(['adb','shell','uiautomator','dump','/sdcard/eff-test-ui.xml'],stdout=subprocess.DEVNULL,check=True)
raw=subprocess.check_output(['adb','shell','cat','/sdcard/eff-test-ui.xml'])
nodes=list(ET.fromstring(raw).iter('node'))
if args.action=='list':
 for n in nodes:
  if n.get('text'):print(ascii(n.get('text')),n.get('bounds'))
else:
 matching=[n for n in nodes if n.get('text')==args.text]
 if len(matching)!=1:raise SystemExit('Expected exactly one visible match, found '+str(len(matching)))
 box=list(map(int,re.findall(r'\d+',matching[0].get('bounds'))))
 subprocess.run(['adb','shell','input','tap',str((box[0]+box[2])//2),str((box[1]+box[3])//2)],check=True)
 time.sleep(.8)
