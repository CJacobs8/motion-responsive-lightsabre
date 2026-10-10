function [X, valid] = buildFeatureMatrix(D, pre, post, fs, removeGravity)
% BUILDFEATUREMATRIX  Features for every clash using window [anchor-pre, anchor+post).
if nargin < 4, fs = 500; end
if nargin < 5, removeGravity = false; end
M = size(D.win, 3);
rows = (D.pre - pre + 1):(D.pre + post);
baseRows = (D.pre - 64 + 1):(D.pre - 14);       % 50 samples before impact
X = nan(M, 74);
valid = false(M, 1);
for i = 1:M
    w = D.win(rows, :, i);
    if removeGravity
        g = mean(D.win(baseRows, 1:3, i), 1);
        w(:, 1:3) = w(:, 1:3) - g;
    end
    if all(isfinite(w(:)))
        X(i, :) = clashFeatures(w, fs);
        valid(i) = true;
    end
end
end
