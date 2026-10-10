function [P, f] = welchSpectrum(x, fs)
% WELCHSPECTRUM  Averaged power spectrum of a short signal (Welch method).
FRAME_LEN = 16;  FRAME_HOP = 8;  NFFT = 64;
x = x(:);
n = numel(x);
w = 0.54 - 0.46 * cos(2*pi*(0:FRAME_LEN-1)' / (FRAME_LEN-1));   % Hamming
starts = 1:FRAME_HOP:(n - FRAME_LEN + 1);
P = zeros(NFFT/2, 1);
for s = starts
    fr = x(s:s+FRAME_LEN-1);
    fr = (fr - mean(fr)) .* w;
    X = fft(fr, NFFT);
    P = P + abs(X(2:NFFT/2+1)).^2;
end
P = P / numel(starts);
f = (1:NFFT/2)' * fs / NFFT;
end
