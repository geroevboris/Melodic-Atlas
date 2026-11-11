#!/usr/bin/env python3
"""Validate the shipped 145x512 compressed text vectors. Stdlib only."""
import base64,math,re,struct,unittest
from pathlib import Path
HEADER=Path(__file__).resolve().parents[1]/"Source"/"ClapHierarchy05Bank.h"
def parse():
    text=HEADER.read_text(encoding="utf-8")
    m=re.search(r'static constexpr const char\* encodedQ15\s*=\s*(.*?);',text,re.S)
    assert m, "Missing base64 text bank"
    chunks=re.findall(r'"([A-Za-z0-9+/=]+)"',m.group(1))
    raw=base64.b64decode("".join(chunks),validate=True)
    m2=re.search(r'static constexpr std::array<PromptInfo,promptCount> prompts\s*\{\{(.*?)\}\};',text,re.S)
    assert m2, "Missing prompt metadata"
    info=[(int(a),int(b),int(c),float(d)) for a,b,c,d in re.findall(
        r'\{\s*(\d+),\s*(\d+),\s*(\d+),\s*([\d.eE+\-]+)f\s*\}',
        m2.group(1))]
    return raw,info
class Hierarchy05Test(unittest.TestCase):
    def test_size_and_groups(self):
        data,info=parse()
        self.assertEqual(len(data),145*512*2)
        self.assertEqual(len(info),145)
        self.assertEqual(len(set(m[1] for m in info)),65)
        self.assertEqual(set(m[0] for m in info),set(range(8)))
        self.assertTrue(all(p==0 for f,s,p,k in info if f==7))
        self.assertTrue(all(p>0 for f,s,p,k in info if f<7))
        self.assertTrue(all(0<=p<=24 and 0<=s<65 for f,s,p,k in info))
    def test_reconstruction(self):
        raw,meta=parse()
        for i,(_fam,_sub,_parent,scale) in enumerate(meta):
            self.assertTrue(math.isfinite(scale) and 0<scale<1e-3)
            values=struct.unpack_from("<512h",raw,i*1024)
            norm=math.sqrt(sum((v*scale)**2 for v in values))
            self.assertAlmostEqual(norm,1.0,delta=.002)
        self.assertAlmostEqual(struct.unpack_from("<h",raw,0)[0]*meta[0][3],
                               .00035262,delta=.00001)
        self.assertAlmostEqual(struct.unpack_from("<h",raw,2)[0]*meta[0][3],
                               -.02808110,delta=.00001)
if __name__=="__main__":unittest.main()
