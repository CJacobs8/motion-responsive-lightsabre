function [yPred, P] = crossValidateSoftmax(X, y, groups, lambda)
% CROSSVALIDATESOFTMAX  Out-of-fold predictions.
yPred = zeros(size(y));
P = zeros(numel(y), 3);
g = unique(groups);
for k = 1:numel(g)
    test = groups == g(k);
    model = trainSoftmax(X(~test, :), y(~test), lambda);
    [yPred(test), P(test, :)] = predictSoftmax(model, X(test, :));
end
end
