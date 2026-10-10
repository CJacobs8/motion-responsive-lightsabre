function [label, P] = predictSoftmax(model, X)
% PREDICTSOFTMAX  Class (0..C-1) and probabilities. Same maths as
Z = (X - model.mu) ./ model.sigma;
S = Z * model.W' + model.b';
S = S - max(S, [], 2);
P = exp(S);
P = P ./ sum(P, 2);
[~, idx] = max(P, [], 2);
label = idx - 1;
end
