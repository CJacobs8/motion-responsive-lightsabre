function model = trainSoftmax(X, y, lambda, nClasses)
% TRAINSOFTMAX  L2-regularised multinomial logistic regression.

if nargin < 4, nClasses = 3; end
[N, F] = size(X);

mu = mean(X, 1);
sigma = std(X, 0, 1);
sigma(sigma < 1e-9) = 1;
Z = (X - mu) ./ sigma;

Y = zeros(N, nClasses);
Y(sub2ind(size(Y), (1:N)', y + 1)) = 1;

Lc = 0.5 * norm(Z)^2 / N + lambda;
step = 1 / Lc;

W = zeros(nClasses, F);  b = zeros(nClasses, 1);
Wv = W;  bv = b;         % look-ahead point
t = 1;

for it = 1:20000
    S = Zv_scores(Z, Wv, bv);
    P = softmaxRows(S);
    G = (P - Y) / N;                 % N x C
    gW = G' * Z + lambda * Wv;
    gb = sum(G, 1)';

    Wn = Wv - step * gW;
    bn = bv - step * gb;

    tn = (1 + sqrt(1 + 4*t^2)) / 2;
    Wv = Wn + ((t - 1) / tn) * (Wn - W);
    bv = bn + ((t - 1) / tn) * (bn - b);

    if max(abs(Wn(:) - W(:))) < 1e-7 && it > 50
        W = Wn; b = bn; break;
    end
    W = Wn;  b = bn;  t = tn;
end

model.W = W;
model.b = b;
model.mu = mu;
model.sigma = sigma;
model.lambda = lambda;
model.iterations = it;
end

function S = Zv_scores(Z, W, b)
S = Z * W' + b';
end

function P = softmaxRows(S)
S = S - max(S, [], 2);
E = exp(S);
P = E ./ sum(E, 2);
end
