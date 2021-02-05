from netgen.geom2d import unit_square
from ngsolve import Mesh
from tentswebgui import Draw
from ngstents import TentSlab

mesh = Mesh(unit_square.GenerateMesh(maxh=0.2))
print("Fetching tent data")

# using causality constant
local_ctau = True
global_ctau = 1
wavespeed = 2
dt = 1.0
ts = TentSlab(mesh, method="edge")
ts.SetWavespeed(wavespeed)
ts.PitchTents(dt=dt, local_ct=local_ctau, global_ct=global_ctau)
print("max slope", ts.MaxSlope())
print("n tents", ts.GetNTents())
print("Generating tents.html")
Draw(ts, 'tents.html')
