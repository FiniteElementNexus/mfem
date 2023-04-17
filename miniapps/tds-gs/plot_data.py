import numpy as np
import matplotlib.pyplot as plt


fbar = [1, 0.99199, 0.983911, 0.975783, 0.967616, 0.959415, 0.951183, 0.942926, 0.934644, 0.926339, 0.918014, 0.909669, 0.901305, 0.892923, 0.884525, 0.876111, 0.867681, 0.859235, 0.850776, 0.842302, 0.833815, 0.825314, 0.816801, 0.808275, 0.799737, 0.791187, 0.782625, 0.774051, 0.765466, 0.756869, 0.748262, 0.739644, 0.731016, 0.722377, 0.713728, 0.705069, 0.696399, 0.687719, 0.679028, 0.670328, 0.661619, 0.652901, 0.644177, 0.635445, 0.626704, 0.617952, 0.60919, 0.600417, 0.591629, 0.582828, 0.574013, 0.565192, 0.556369, 0.547551, 0.538747, 0.52997, 0.521236, 0.512558, 0.503949, 0.495419, 0.486975, 0.478626, 0.47038, 0.462238, 0.454199, 0.446266, 0.438436, 0.43071, 0.423082, 0.415553, 0.408117, 0.400773, 0.393521, 0.38636, 0.37929, 0.372312, 0.365424, 0.358626, 0.351918, 0.345298, 0.338767, 0.332322, 0.325965, 0.319692, 0.313504, 0.307401, 0.30138, 0.295441, 0.289584, 0.283808, 0.278111, 0.272494, 0.266955, 0.261494, 0.25611, 0.250802, 0.245571, 0.240413, 0.235331, 0.230321, 0.225383, 0.220517, 0.215722, 0.210998, 0.206343, 0.201757, 0.197238, 0.192787, 0.188403, 0.184085, 0.179832, 0.175643, 0.171518, 0.167457, 0.163457, 0.159519, 0.155642, 0.151825, 0.148068, 0.14437, 0.14073, 0.137147, 0.133622, 0.130153, 0.126739, 0.12338, 0.120075, 0.116824, 0.113626, 0.11048, 0.107386, 0.104343, 0.101351, 0.0984078, 0.0955144, 0.092669, 0.0898723, 0.0871224, 0.0844198, 0.0817633, 0.0791522, 0.0765866, 0.074065, 0.0715876, 0.0691536, 0.0667617, 0.0644126, 0.0621056, 0.0598387, 0.0576134, 0.0554275, 0.0532811, 0.0511742, 0.0491055, 0.0470743, 0.0450813, 0.0431251, 0.0412052, 0.0393214, 0.0374725, 0.0356592, 0.0338801, 0.0321352, 0.0304239, 0.0287455, 0.0270993, 0.0254861, 0.0239038, 0.0223531, 0.0208333, 0.0193438, 0.0178846, 0.0164543, 0.015053, 0.0136813, 0.0123366, 0.0110209, 0.00973211, 0.00847035, 0.00723557, 0.00602712, 0.00484434, 0.00368657, 0.00255447, 0.00144738, 0.000363983, -0.000694399, -0.00172974, -0.00274205, -0.00373067, -0.00469756, -0.00564141, -0.00656355, -0.00746462, -0.00834397, -0.00920292, -0.0100402, -0.0108583, -0.011656, -0.012434, -0.0131923, -0.0139321, -0.0146521, -0.0153544, -0.0160376, -0.0167031, -0.0173508, -0.0179806, -0.0185928, -0.0191878, -0.0197657, -0.0203265, -0.0208708, -0.021398, -0.0219094, -0.0224044, -0.0228836, -0.0233463, -0.0237939, -0.0242263, -0.0246429, -0.0250444, -0.0254308, -0.0258027, -0.0261594, -0.0265023, -0.0268301, -0.0271441, -0.0274436, -0.0277286, -0.0279991, -0.0282558, -0.028498, -0.0287257, -0.0289383, -0.0291364, -0.0293181, -0.0294813, -0.0296235, -0.0297387, -0.029817, -0.0298433, -0.0297881, -0.029611, -0.0292266, -0.0285342, -0.027408, -0.025725, -0.023468, -0.0206997, -0.0175923, -0.0143592, -0.011215, -0.00839268, -0.00593036, -0.0033397, -0]
fbar_prime = [-2.04187, -2.05939, -2.07447, -2.08576, -2.09511, -2.10337, -2.11061, -2.1171, -2.12308, -2.12864, -2.13378, -2.13875, -2.14347, -2.14777, -2.15198, -2.15611, -2.16007, -2.16378, -2.1674, -2.17102, -2.17448, -2.17776, -2.18105, -2.18425, -2.18728, -2.19031, -2.19343, -2.19638, -2.19924, -2.20211, -2.2048, -2.2075, -2.21011, -2.21281, -2.2155, -2.21812, -2.22081, -2.22351, -2.22604, -2.22839, -2.23059, -2.23252, -2.23446, -2.23657, -2.23901, -2.24171, -2.24457, -2.24786, -2.25139, -2.25485, -2.25738, -2.25847, -2.25805, -2.25561, -2.2503, -2.24137, -2.22882, -2.21272, -2.19377, -2.17271, -2.14945, -2.12418, -2.09772, -2.0711, -2.04439, -2.01769, -1.99123, -1.96528, -1.94009, -1.91558, -1.89173, -1.86831, -1.84489, -1.82147, -1.79822, -1.77488, -1.75171, -1.7288, -1.70605, -1.68339, -1.66089, -1.63865, -1.61666, -1.59492, -1.57327, -1.55187, -1.53081, -1.50992, -1.48911, -1.46855, -1.44816, -1.42794, -1.40797, -1.38818, -1.36855, -1.349, -1.32979, -1.31075, -1.29188, -1.27326, -1.25481, -1.23661, -1.2185, -1.20055, -1.18286, -1.16542, -1.14807, -1.13088, -1.11394, -1.09709, -1.0805, -1.06415, -1.04789, -1.03189, -1.01605, -1.00029, -0.98479, -0.969457, -0.954292, -0.939296, -0.924468, -0.909808, -0.895318, -0.881079, -0.866926, -0.85294, -0.839123, -0.825475, -0.811995, -0.798684, -0.785625, -0.772566, -0.759676, -0.747039, -0.73457, -0.722185, -0.709969, -0.697921, -0.685958, -0.674247, -0.662621, -0.651163, -0.639874, -0.628668, -0.617716, -0.606848, -0.59598, -0.585449, -0.575002, -0.564639, -0.554529, -0.544419, -0.534478, -0.524789, -0.5151, -0.505496, -0.496144, -0.486877, -0.477778, -0.468763, -0.459833, -0.451071, -0.442393, -0.433884, -0.425543, -0.417203, -0.40903, -0.401027, -0.393023, -0.385188, -0.377437, -0.369855, -0.362441, -0.354942, -0.347697, -0.340536, -0.333375, -0.326466, -0.319558, -0.312733, -0.306078, -0.299591, -0.293103, -0.286616, -0.280382, -0.274147, -0.267997, -0.2621, -0.256118, -0.250305, -0.244576, -0.238847, -0.233371, -0.227894, -0.222502, -0.217111, -0.211887, -0.206832, -0.201693, -0.196638, -0.191751, -0.186865, -0.182063, -0.177345, -0.172627, -0.168077, -0.163528, -0.158978, -0.154513, -0.150132, -0.145751, -0.141455, -0.137158, -0.132945, -0.128817, -0.124689, -0.120561, -0.116517, -0.112641, -0.108682, -0.104722, -0.100846, -0.0970552, -0.093264, -0.089557, -0.0858501, -0.0821431, -0.0785204, -0.0748134, -0.0711064, -0.0674837, -0.063861, -0.060154, -0.0563628, -0.0525716, -0.0486119, -0.0441466, -0.0390917, -0.0329415, -0.0247693, -0.0133956, 0.00370697, 0.02974, 0.0718647, 0.137832, 0.232781, 0.359576, 0.504316, 0.643244, 0.752094, 0.811574, 0.816292, 0.76372, 0.676438, 0.646782, 0.759086, 0.950838]
fbar_double_prime = [-0.00775094, -4.48611, -3.23517, -2.545, -2.24305, -1.98424, -1.72543, -1.59602, -1.46661, -1.38034, -1.25093, -1.29407, -1.12153, -1.07839, -1.07839, -1.03526, -0.99212, -0.905849, -0.948984, -0.905849, -0.862713, -0.819577, -0.862713, -0.776442, -0.776442, -0.776442, -0.819577, -0.69017, -0.776442, -0.69017, -0.69017, -0.69017, -0.647035, -0.733306, -0.647035, -0.69017, -0.69017, -0.69017, -0.603899, -0.603899, -0.517628, -0.474492, -0.517628, -0.560764, -0.69017, -0.69017, -0.776442, -0.905849, -0.905849, -0.862713, -0.431357, -0.129407, 0.345085, 0.905849, 1.8117, 2.76068, 3.66653, 4.57238, 5.13314, 5.65077, 6.25467, 6.68603, 6.85857, 6.7723, 6.9017, 6.7723, 6.7723, 6.51348, 6.38408, 6.1684, 6.03899, 5.95272, 6.03899, 5.95272, 5.95272, 5.99586, 5.86645, 5.86645, 5.78018, 5.82331, 5.69391, 5.69391, 5.5645, 5.5645, 5.52136, 5.43509, 5.34882, 5.34882, 5.30569, 5.21941, 5.21941, 5.13314, 5.09001, 5.04687, 5.00374, 5.00374, 4.83119, 4.91746, 4.74492, 4.78806, 4.65865, 4.65865, 4.61552, 4.57238, 4.48611, 4.44297, 4.44297, 4.3567, 4.31357, 4.31357, 4.18416, 4.18416, 4.14102, 4.05475, 4.05475, 4.01162, 3.92534, 3.92534, 3.83907, 3.83907, 3.7528, 3.7528, 3.66653, 3.62339, 3.62339, 3.53712, 3.53712, 3.45085, 3.45085, 3.36458, 3.32145, 3.36458, 3.23517, 3.23517, 3.1489, 3.19204, 3.06263, 3.10577, 3.0195, 2.97636, 2.97636, 2.89009, 2.89009, 2.84695, 2.76068, 2.80382, 2.76068, 2.63127, 2.71755, 2.58814, 2.58814, 2.58814, 2.50187, 2.45873, 2.50187, 2.4156, 2.37246, 2.37246, 2.28619, 2.32933, 2.24305, 2.24305, 2.19992, 2.15678, 2.11365, 2.15678, 2.02738, 2.07051, 2.02738, 1.98424, 1.98424, 1.89797, 1.89797, 1.9411, 1.76856, 1.89797, 1.76856, 1.76856, 1.76856, 1.72543, 1.68229, 1.63915, 1.68229, 1.63915, 1.55288, 1.63915, 1.50975, 1.50975, 1.55288, 1.42348, 1.50975, 1.42348, 1.38034, 1.42348, 1.33721, 1.42348, 1.25093, 1.33721, 1.29407, 1.29407, 1.2078, 1.29407, 1.16466, 1.25093, 1.16466, 1.16466, 1.16466, 1.16466, 1.12153, 1.12153, 1.12153, 1.07839, 1.12153, 1.03526, 1.07839, 1.03526, 1.07839, 0.99212, 0.99212, 1.03526, 0.99212, 0.99212, 0.948984, 0.99212, 0.905849, 0.99212, 0.905849, 0.948984, 0.948984, 0.948984, 0.905849, 0.948984, 0.948984, 0.99212, 0.948984, 1.07839, 1.2078, 1.38034, 1.76856, 2.4156, 3.40772, 5.34882, 7.9801, 13.5877, 20.1875, 28.4264, 36.4928, 37.6143, 33.5164, 22.2149, 8.23891, -5.82331, -21.0933, -23.5952, 8.41145, 49.0884, -0.126037]

plt.figure()
plt.plot(fbar)
plt.figure()
plt.plot(fbar_prime)
plt.figure()
plt.plot(fbar_double_prime)
plt.show()

ffprim = []
pprim = []
rzbbs = []
with open("fpol_pres_ffprim_pprime.data", 'r') as fid:
    for line in fid:
        out = line.split(" ")
        ffprim.append(eval(out[2]))
        pprim.append(eval(out[3]))

with open("separated_file.data", 'r') as fid:
    for line in fid:
        if "rbbbs" in line:
            out = fid.readline()[:-2].split(" ")
            for num in out:
                rzbbs.append(eval(num))
                
rzbbs = np.array(rzbbs)
rbbbs = rzbbs[::2]
zbbbs = rzbbs[1::2]
plt.figure()
plt.plot(rbbbs, zbbbs)
plt.show()
breakpoint()
        
plt.figure()
plt.plot(ffprim)
plt.ylabel("ffprime")
plt.xlabel("psi_N")
plt.figure()
plt.plot(pprim)
plt.ylabel("pprime")
plt.xlabel("psi_N")
plt.show()

