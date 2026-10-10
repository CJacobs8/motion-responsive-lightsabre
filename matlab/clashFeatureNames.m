function names = clashFeatureNames()
% CLASHFEATURENAMES  Names of the 74 features from clashFeatures.m
sig  = {'Ax','Ay','Az','Gx','Gy','Gz','Amag','Gmag'};
per  = {'Mean','Std','MaxAbs','RMS','Band1','Band2','Band3','Band4','Centroid'};
names = strings(1, 8*9 + 2);
c = 1;
for s = 1:8
    for p = 1:9
        names(c) = sig{s} + "_" + per{p};
        c = c + 1;
    end
end
names(c)   = "GyroAccelPeakRatio";
names(c+1) = "PeakLag_ms";
end
