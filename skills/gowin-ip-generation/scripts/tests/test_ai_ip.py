import importlib.util,sys,unittest,subprocess,json,tempfile,fcntl
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
spec=importlib.util.spec_from_file_location('ai_ip',ROOT/'gowin_ip.py');m=importlib.util.module_from_spec(spec);spec.loader.exec_module(m)
class AdapterTests(unittest.TestCase):
 def test_inventory_and_duplicate_identity(self):
  self.assertEqual(len(m.inventory()),271)
  with self.assertRaises(m.RequestError) as e:m.resolve('digital_pre-distortion')
  self.assertEqual(e.exception.payload['status'],'AMBIGUOUS_IP')
  self.assertEqual(m.resolve('MIPI_CPHY_RX/mipi_cphy_rx.ipspec')['directory'],'MIPI_CPHY_RX')
 def test_types_and_bounds(self):
  f=[{'object':'enable','key':'group/enable','class':'QCheckBox','enabled':True},{'object':'width','class':'QSpinBox','enabled':True,'min':1,'max':32}]
  self.assertEqual(m.validate_parameters({'enable':True,'width':18},f)['width'],18)
  for params in [{'enable':'false'},{'width':33},{'width':True},{'width':2.5}]:
   with self.assertRaises(m.RequestError):m.validate_parameters(params,f)
 def test_duplicate_widget_requires_qualified_key(self):
  f=[{'object':'n','key':'a/n','class':'QLineEdit','enabled':True},{'object':'n','key':'b/n','class':'QLineEdit','enabled':True}]
  with self.assertRaises(m.RequestError):m.validate_parameters({'n':'value'},f)
  self.assertEqual(m.validate_parameters({'b/n':'value'},f),{'b/n':'value'})
 def test_status_not_an_unusable_claim(self):
  self.assertEqual(m.classify({'returncode':124},{'visible_surfaces':[{'class':'Custom'}]}),'UNHANDLED_DIALOG')
  self.assertEqual(m.classify({}, {'status':'GENERATOR_MESSAGE','dialog_text':'Coefficient file should be specified.'}),'INPUT_REQUIRED')
  self.assertEqual(m.classify({}, {'status':'NOT_AVAILABLE_FOR_TARGET'}),'CATALOG_ENTRY_UNRESOLVED')
 def test_modulegen_shutdown_does_not_imply_failure(self):
  self.assertEqual(m.classify({'generation_pass':True},{'status':'APPLICATION_EXITED_AFTER_ACCEPT'}),'GENERATED')
 def test_exact_target_and_no_silent_version_selection(self):
  ip=m.resolve('FFT/fft.ipspec');d=m.choose_device(ip,'GW5AST-LV138PG484AC1/I0','C');self.assertEqual(d['id'],'gw5ast138c-007')
 def test_parameters_cannot_escape_output_directory(self):
  for key in ['editCreateIn','group/editCreateIn','editModuleName','lineEditFileName']:
   with self.assertRaises(m.RequestError) as e:m.validate_parameters({key:'/different/project'},[])
   self.assertEqual(e.exception.payload['status'],'RESERVED_PARAMETER')
 def test_dependent_bounds_deferred_to_live_setter(self):
  fields=[{'object':'width','class':'QSpinBox','enabled':True,'min':1,'max':32}]
  self.assertEqual(m.validate_parameters({'width':64},fields,check_constraints=False),{'width':64})
  with self.assertRaises(m.RequestError):m.validate_parameters({'width':True},fields,check_constraints=False)
 def test_missing_widget_is_not_verified(self):
  self.assertTrue(m.parameter_mismatches({'width':18},[]))
 def test_raw_json_object_required(self):
  with self.assertRaises(m.RequestError):m.parse_parameters('[1,2]')
class CliTests(unittest.TestCase):
 def call(self,*args):
  p=subprocess.run([str(ROOT/'bin/gowin-ip-ai'),*args],capture_output=True,text=True,timeout=15)
  return p.returncode,json.loads(p.stdout)
 def test_missing_file_does_not_launch_or_write(self):
  with tempfile.TemporaryDirectory() as temp:
   out=Path(temp)/'unused';rc,x=self.call('generate','--ip','ADVANCED_FIR/ad_fir.ipspec','--output',str(out))
   self.assertEqual(x['status'],'INPUT_REQUIRED');self.assertFalse(out.exists())
 def test_file_widgets_use_explicit_input_mapping(self):
  with tempfile.TemporaryDirectory() as temp:
   rc,x=self.call('generate','--ip','ADVANCED_FIR/ad_fir.ipspec','--params','{"lineEditCoefficientFile":"coeff.txt"}','--output',str(Path(temp)/'unused'))
   self.assertEqual(x['status'],'FILE_INPUT_REQUIRES_MAPPING')
 def test_nonexistent_file_rejected(self):
  rc,x=self.call('generate','--ip','EDID_PROM/edid_prom.ipspec','--input','initialization=/nonexistent/edid.mi','--output','/tmp/unused_gowin_test_output')
  self.assertEqual(x['status'],'INPUT_FILE_INVALID')
 def test_json_request_without_shell_interpolation(self):
  with tempfile.TemporaryDirectory() as temp:
   p=Path(temp)/'request.json';p.write_text(json.dumps({'action':'describe','ip':'FFT/fft.ipspec'}))
   rc,x=self.call('run','--request',str(p));self.assertEqual(rc,0);self.assertEqual(x['status'],'DESCRIPTION')
 def test_unknown_request_action_rejected(self):
  with tempfile.TemporaryDirectory() as temp:
   p=Path(temp)/'request.json';p.write_text(json.dumps({'action':'pnr','ip':'FFT/fft.ipspec'}))
   rc,x=self.call('run','--request',str(p));self.assertEqual(x['status'],'INVALID_ACTION')
 def test_vendor_stub_has_binary_evidence(self):
  rc,x=self.call('inspect','--ip','MIPI_CPHY_RX/mipi_cphy_rx.ipspec','--module','unused','--output','/tmp/unused_gowin_stub_test')
  self.assertEqual(x['status'],'VENDOR_ENTRY_STUB');self.assertTrue(x['evidence'][0]['sha256'])
if __name__=='__main__':unittest.main()
