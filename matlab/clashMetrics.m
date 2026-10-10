function R = clashMetrics(yTrue, yPred, P)
% CLASHMETRICS  Accuracy, per-class precision/recall/F1, macro F1, AUC.
C = 3;
R.confusion = zeros(C);
for i = 1:numel(yTrue)
    R.confusion(yTrue(i)+1, yPred(i)+1) = R.confusion(yTrue(i)+1, yPred(i)+1) + 1;
end
R.accuracy  = 100 * mean(yTrue == yPred);
tp = diag(R.confusion);
R.precision = tp ./ max(sum(R.confusion, 1)', 1);
R.recall    = tp ./ max(sum(R.confusion, 2), 1);
R.f1        = 2 * R.precision .* R.recall ./ max(R.precision + R.recall, eps);
R.macroF1   = mean(R.f1);
R.n         = numel(yTrue);
p = R.accuracy / 100;  z = 1.96;  n = R.n;
centre = (p + z^2/(2*n)) / (1 + z^2/n);
half   = z * sqrt(p*(1-p)/n + z^2/(4*n^2)) / (1 + z^2/n);
R.accuracyCI = 100 * [centre - half, centre + half];
R.auc = nan(C, 1);
if nargin >= 3 && ~isempty(P) && exist('perfcurve', 'file')
    for c = 1:C
        if any(yTrue == c-1) && any(yTrue ~= c-1)
            [~, ~, ~, R.auc(c)] = perfcurve(yTrue == c-1, P(:, c), true);
        end
    end
end
end
