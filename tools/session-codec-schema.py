# SPDX-License-Identifier: AGPL-3.0-or-later
# Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko & Alisa Lafoks. Part of felitronics-mastering-core — see LICENSE.

# Regenerate the named-field description; session-codec.cmake checks it against the C++ records.
import json
from pathlib import Path
schema={}
def add(name,cpp,fields): schema[name]={'cpp':cpp,'fields':fields}
for name,fields in {
'TargetFields':{'lufs':'double','tp':'double'},'HpfFields':{'on':'bool','fq':'double','slope':'int32'},
'MonoBassFields':{'on':'bool','fq':'double','width':'double'},'GlueFields':{'on':'bool','upToDb':'double'},
'SaturationFields':{'on':'bool','drive':'double','mix':'double','output':'double'},'TiltFields':{'on':'bool','db':'double'},
'LimiterFields':{'needles':'Needles','needlesDb':'double'},'DitherFields':{'on':'bool'},'LowShelfFields':{'on':'bool','db':'double'}}.items():
 for form in ['Value','Touched']:
  if name == 'TargetFields' and form == 'Value': continue
  add(name+form,name+'<'+form+'>',{k:v+('?' if form=='Touched' else '') for k,v in fields.items()})
 if name!='TargetFields': add(name+'Layers','Layers<'+name+'>',{'machine':name+'Value','hand':name+'Touched'})
add('Devices','Devices',{k:v+'FieldsLayers' for k,v in {'hpf':'Hpf','monoBass':'MonoBass','glue':'Glue','saturation':'Saturation','tilt':'Tilt','limiter':'Limiter','dither':'Dither','lowShelf':'LowShelf'}.items()})
add('Project','Project',{'target':'uint16','targetEdit':'TargetFieldsTouched','manual':'bool','devices':'Devices'})
add('Recipe','Recipe',{'project':'Project','source':'uint64','sound':'uint64'})
add('Kept','Kept',{'id':'uint32','recipe':'Recipe'})
add('Source','Source',{'channels':'uint32','sampleRate':'uint32','frames':'uint64','hash':'uint64','fileRate':'uint32','rateKnown':'bool','bitDepth':'uint8','name':'string'})
add('Phase','Phase',{'name':'PhaseName','fraction':'double','weightsVersion':'uint64','pass':'uint32','totalPasses':'uint32','completedUnits':'uint32','totalUnits':'uint32'})
add('ReadingPoint','ReadingPoint',{'index':'uint64','value':'double'})
add('ReadingRun','ReadingRun',{'first':'uint64','count':'uint64','value':'double'})
add('Snapshot','SnapshotView',{'state':'State','mastering':'bool','revision':'uint64','project':'Project','target':'string','source':'Source','job':'uint32','measurementJob':'uint32','jobRecipe':'Recipe','masters':'Kept[]','measurementProgress':'Phase','masterProgress':'Phase','sourceBytes':'bytes','integratedLufs':'double','momentary':'ReadingPoint[]','shortTerm':'ReadingPoint[]','runs':'ReadingRun[]'})
Path(__file__).with_name('session-codec-schema.json').write_text(json.dumps({'enums':{'State':['Empty','Loaded','Measured1','Measured2'],'Needles':['Auto','Manual','Off'],'PhaseName':['Stream','Report','Analyzers','Pass','Remeasure']},'records':schema},indent=2)+'\n')
