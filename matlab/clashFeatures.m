function f = clashFeatures(win, fs)
% CLASHFEATURES  Feature vector for one clash window.

if nargin < 2, fs = 500; end

NUM_BANDS = 4;
n = size(win, 1);

accelMag = sqrt(sum(win(:,1:3).^2, 2));
gyroMag  = sqrt(sum(win(:,4:6).^2, 2));
signals  = [win(:,1:6), accelMag, gyroMag];

f = zeros(1, 8*9 + 2);
col = 1;

for s = 1:8
    x = signals(:, s);

    mu = mean(x);
    f(col)   = mu;
    f(col+1) = std(x);              % n-1 normalisation (as firmware)
    f(col+2) = max(abs(x));
    f(col+3) = sqrt(mean(x.^2));

    [P, freq] = welchSpectrum(x, fs);
    K = numel(P);
    bandOfBin = floor(((1:K)' - 1) * NUM_BANDS / K) + 1;   % 1..NUM_BANDS
    mag = sqrt(P);
    for b = 1:NUM_BANDS
        f(col+3+b) = log10(sum(P(bandOfBin == b)) / (16*16) + 1e-6);
    end
    if sum(mag) > 0
        f(col+4+NUM_BANDS) = sum(mag .* freq) / sum(mag);
    else
        f(col+4+NUM_BANDS) = 0;
    end

    col = col + 9;
end

[peakA, ia] = max(accelMag);
[peakG, ig] = max(gyroMag);
f(col)   = log10((peakG + 1e-3) / (peakA + 1e-3));
f(col+1) = (ig - ia) * 1000 / fs;

end
