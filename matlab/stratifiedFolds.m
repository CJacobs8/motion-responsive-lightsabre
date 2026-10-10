function folds = stratifiedFolds(y, k, seed)
% STRATIFIEDFOLDS  Fold number 1..k per sample, balanced per class.
if nargin < 3, seed = 1; end
s = RandStream('mt19937ar', 'Seed', seed);
folds = zeros(size(y));
for c = unique(y)'
    idx = find(y == c);
    idx = idx(randperm(s, numel(idx)));
    folds(idx) = mod(0:numel(idx)-1, k)' + 1;
end
end
