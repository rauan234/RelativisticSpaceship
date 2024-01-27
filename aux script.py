import numpy as np
import csv
from math import tanh


def compute_xyz_to_rgb():
    global xyz_to_rgb
    
    M = np.array([
        [0.67, 0.21, 0.14],
        [0.33, 0.71, 0.08],
        [0, 0, 0]
    ])
    M[2] = np.array([1, 1, 1]) - M[0] - M[1]

    xw = 0.3101
    yw = 0.3162
    white = np.linalg.solve(M, np.array([xw, yw, 1 - xw - yw]))

    xyz_to_rgb = np.linalg.inv(M)
    for i in range(3):
        xyz_to_rgb[i] /= white[i]

def remap_rgb(rgb):
    white = [0.33, 0.33, 0.33]
    
    rgb_normalized = rgb / sum(rgb);
    
    e = max(-rgb[0] / (white[0] + abs(rgb_normalized[0])),
		 -rgb[1] / (white[1] + abs(rgb_normalized[1])),
                 -rgb[2] / (white[2] + abs(rgb_normalized[2])))
    if(e > 0):
        rgb = rgb + e * (white - rgb_normalized)
    
    return [tanh(rgb[0]), tanh(rgb[1]), tanh(rgb[2])]


compute_xyz_to_rgb()

#print(xyz_to_rgb)
#print(xyz_to_rgb.dot(np.array([0.63, 0.34, 0.03])))
#print(xyz_to_rgb.dot(np.array([0.31, 0.59, 0.10])))
#print(xyz_to_rgb.dot(np.array([0.16, 0.07, 0.77])))

n = 0
with open('CIE_xyz_1931_2deg_5nm.csv', newline='') as csvfile:
    spamreader = csv.reader(csvfile)
    for row in spamreader:
        l, x, y, z = row
        x = float(x)
        y = float(y)
        z = float(z)
        
        rgb = xyz_to_rgb.dot(np.array([x, y, z]))
        n += 1
        
        color = remap_rgb(rgb)
        print('{' + ', '.join(list(map(lambda x: format(x, 'f'), color))) + '},')
        #print('RGBColor[' + ', '.join(list(map(lambda x: format(x, 'f'), color))) + '], ')
print(n)
