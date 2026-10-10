%% CLASHPIPELINE  Train, validate, test and export the clash-location classifier.
clear; clc; close all;

DATA_ROOT     = fullfile(fileparts(mfilename('fullpath')), '..', 'data', 'clash_dataset');
TEST_SESSIONS = ["S04","V03"];   % S04 = horizontal test, V03 = vertical test

FS            = 500;
REMOVE_GRAVITY = true;
PEAK_SEARCH   = 8;                % = CC_PEAK_SEARCH in ClashClassifier.h

PRE_LIST      = [8 16 32];
POST_LIST     = [16 24 32 48 64 96];
LAMBDA_SWEEP  = 1e-2;             % lambda used during the window sweep
LAMBDA_LIST   = [1e-4 1e-3 3e-3 1e-2 3e-2 1e-1 3e-1 1];
LATENCY_BUDGET_MS = 150;          % TPM_08 (200 ms) minus margin for audio/LED

EXCLUDE_FEATURES = strings(0);   % e.g. ["Ax_Mean","Ay_Mean","Az_Mean","Gx_Mean","Gy_Mean","Gz_Mean"]

RESULTS = fullfile(fileparts(mfilename('fullpath')), '..', 'results');
set(groot, 'defaultAxesFontSize', 13);      % readable text in the report figures
set(groot, 'defaultTextFontSize', 13);
if ~exist(RESULTS, 'dir'), mkdir(RESULTS); end
zoneNames = {'Bottom', 'Middle', 'Top'};
featNames = clashFeatureNames();
featMask  = ~ismember(featNames, EXCLUDE_FEATURES);
fprintf('Using %d of %d features (excluded: %s)\n', sum(featMask), numel(featMask), ...
    strjoin(EXCLUDE_FEATURES, ', '));

D = loadClashDataset(DATA_ROOT);

for z = 0:2
    if sum(D.label == z) == 0
        error('No %s clashes were loaded - check the file names (BOTTOM_/MIDDLE_/TOP_).', ...
            upper(zoneNames{z+1}));
    end
end

preGyro = squeeze(mean(sqrt(sum(D.win(1:D.pre-10, 4:6, :).^2, 2)), 1, 'omitnan'));
fprintf('\nConfound check (per zone mean +/- std)\n');
fprintf('%-8s %16s %20s\n', 'Zone', 'peak |a| (g)', 'pre-impact |w| (dps)');
for z = 0:2
    m = D.label == z;
    fprintf('%-8s %8.2f +/- %4.2f %12.1f +/- %5.1f\n', zoneNames{z+1}, ...
        mean(D.peakG(m)), std(D.peakG(m)), mean(preGyro(m)), std(preGyro(m)));
end
Xc = [D.peakG, preGyro];
okc = all(isfinite(Xc), 2);
yc = crossValidateSoftmax(Xc(okc,:), D.label(okc), stratifiedFolds(D.label(okc), 10, 2), 1e-3);
confAcc = 100 * mean(yc == D.label(okc));
fprintf('Strength/speed-only classifier: %.1f %% (chance = 33 %%)\n', confAcc);
if confAcc > 50
    warning('Zones differ in strike strength or swing speed - results will be optimistic.');
end

logs = dir(fullfile(DATA_ROOT, '**', 'clash_log.csv'));
for i = 1:numel(logs)
    o = detectImportOptions(fullfile(logs(i).folder, logs(i).name));
    o = setvartype(o, {'zone','strength','face','file','time'}, 'string');
    L = readtable(fullfile(logs(i).folder, logs(i).name), o);
    [~, sname] = fileparts(logs(i).folder);
    fprintf('Session %-5s fs %.1f-%.1f Hz | strengths L/M/H %d/%d/%d | faces F/B/L/R %d/%d/%d/%d\n', ...
        sname, min(L.fs_hz), max(L.fs_hz), sum(L.strength=="LIGHT"), sum(L.strength=="MEDIUM"), ...
        sum(L.strength=="HARD"), sum(L.face=="FRONT"), sum(L.face=="BACK"), ...
        sum(L.face=="LEFT"), sum(L.face=="RIGHT"));
    if any(L.fs_hz < 490 | L.fs_hz > 510)
        warning('Session %s contains clashes with a wrong sample rate.', sname);
    end
end

isTest  = ismember(D.session, TEST_SESSIONS);
isTrain = ~isTest;
trainSessions = unique(D.session(isTrain));

fprintf('\nTraining sessions: %s\n', strjoin(trainSessions, ', '));
fprintf('Test sessions    : %s\n', strjoin(unique(D.session(isTest)), ', '));
fprintf('Training clashes : %d   Test clashes: %d\n', sum(isTrain), sum(isTest));

if numel(trainSessions) >= 3
    [~, ~, cvGroups] = unique(D.session(isTrain));
    cvName = 'leave-one-session-out';
else
    cvGroups = stratifiedFolds(D.label(isTrain), 10, 1);
    cvName = 'stratified 10-fold';
end
fprintf('Validation       : %s\n', cvName);

featFiles = dir(fullfile(DATA_ROOT, '**', '*_feat.csv'));
if ~isempty(featFiles)
    maxRel = 0;  checked = 0;
    for i = 1:numel(featFiles)
        csvName = replace(featFiles(i).name, '_feat.csv', '.csv');
        k = find(D.file == csvName, 1);
        if isempty(k), continue; end
        fw = readmatrix(fullfile(featFiles(i).folder, featFiles(i).name));
        pre = 16; post = 48;            % default model window in firmware
        rows = (D.pre - pre + 1):(D.pre + post);
        ml = clashFeatures(D.win(rows, :, k), FS);
        rel = abs(ml - fw) ./ max(abs(ml), 1e-3);
        maxRel = max(maxRel, max(rel));
        checked = checked + 1;
    end
    fprintf('\nFeature parity: %d clashes checked, max relative difference = %.2e\n', ...
        checked, maxRel);
    if maxRel > 2e-3
        warning('MATLAB and firmware features differ - check both implementations.');
    end
end

t_ms = ((1:size(D.win, 1)) - D.pre - 1) / FS * 1000;
amag = squeeze(sqrt(sum(D.win(:, 1:3, :).^2, 2)));
gmag = squeeze(sqrt(sum(D.win(:, 4:6, :).^2, 2)));

fig = figure('Name', 'Mean clash response', 'Position', [100 100 900 600]);
tiledlayout(2, 1, 'TileSpacing', 'compact');
for panel = 1:2
    nexttile; hold on;
    if panel == 1, S = amag; yl = '|a| (g)'; else, S = gmag; yl = '|\omega| (deg/s)'; end
    for z = 0:2
        m = mean(S(:, D.label == z), 2, 'omitnan');
        s = std(S(:, D.label == z), 0, 2, 'omitnan');
        co = colororder;
        fill([t_ms, fliplr(t_ms)], [m' + s', fliplr(m' - s')], ...
            co(z+1, :), 'FaceAlpha', 0.15, 'EdgeColor', 'none', ...
            'HandleVisibility', 'off');
        plot(t_ms, m, 'Color', co(z+1, :), 'LineWidth', 1.6, ...
            'DisplayName', zoneNames{z+1});
    end
    xline(0, ':', 'HandleVisibility', 'off');
    ylabel(yl); grid on; xlim([-40 200]);
    if panel == 1, legend('Location', 'northeast'); title('Mean clash response (± 1 std), aligned on |a| peak'); end
end
xlabel('Time relative to |a| peak (ms)');
saveFig(fig, RESULTS, 'fig_mean_response');

fig = figure('Name', 'Mean spectrum', 'Position', [100 100 900 420]); hold on;
rows = (D.pre - 16 + 1):(D.pre + 48);
for z = 0:2
    idx = find(D.label == z);
    spec = 0; cnt = 0;
    for i = idx'
        x = sqrt(sum(D.win(rows, 1:3, i).^2, 2));
        if any(~isfinite(x)), continue; end
        [Pw, f] = welchSpectrum(x, FS);
        spec = spec + sqrt(Pw);  cnt = cnt + 1;
    end
    plot(f, spec / cnt, 'LineWidth', 1.8, 'DisplayName', zoneNames{z+1});
end
xlabel('Frequency (Hz)'); ylabel('Mean |X(k)| of |a|'); grid on; legend;
title('Mean spectrum of |a| per zone (Hamming-windowed frames)');
saveFig(fig, RESULTS, 'fig_mean_spectrum');

fprintf('\nWindow sweep (%s, lambda = %g)\n', cvName, LAMBDA_SWEEP);
sweepAcc = nan(numel(PRE_LIST), numel(POST_LIST));
yTrainAll = D.label(isTrain);

for a = 1:numel(PRE_LIST)
    for b = 1:numel(POST_LIST)
        [X, valid] = buildFeatureMatrix(D, PRE_LIST(a), POST_LIST(b), FS, REMOVE_GRAVITY);
        X = X(:, featMask);
        Xt = X(isTrain, :);  v = valid(isTrain);
        yp = crossValidateSoftmax(Xt(v, :), yTrainAll(v), cvGroups(v), LAMBDA_SWEEP);
        sweepAcc(a, b) = 100 * mean(yp == yTrainAll(v));
        fprintf('  pre %3d  post %3d  -> %6.2f %%  (latency %3.0f ms)\n', ...
            PRE_LIST(a), POST_LIST(b), sweepAcc(a, b), ...
            (PEAK_SEARCH + POST_LIST(b)) / FS * 1000);
    end
end

latency = (PEAK_SEARCH + POST_LIST) / FS * 1000;
fig = figure('Name', 'Window sweep', 'Position', [100 100 700 400]); hold on;
for a = 1:numel(PRE_LIST)
    plot(latency, sweepAcc(a, :), '-o', 'LineWidth', 1.5, ...
        'DisplayName', sprintf('%d pre-peak samples', PRE_LIST(a)));
end
yline(80, '--', 'TPM\_07 target (80 %)', 'HandleVisibility', 'off');
xline(LATENCY_BUDGET_MS, ':', 'latency budget', 'HandleVisibility', 'off');
xlabel('Classification latency after impact (ms)');
ylabel('Cross-validated accuracy (%)');
title('Accuracy vs window length'); grid on; legend('Location', 'southeast');
saveFig(fig, RESULTS, 'fig_window_sweep');

inBudget = repmat(latency <= LATENCY_BUDGET_MS, numel(PRE_LIST), 1);
cand = sweepAcc;  cand(~inBudget) = -Inf;
best = max(cand(:));
[ra, rb] = find(cand >= best - 1);
[~, k] = min(POST_LIST(rb) * 1000 + PRE_LIST(ra));   % shortest post, then pre
PRE  = PRE_LIST(ra(k));
POST = POST_LIST(rb(k));
fprintf('\nSelected window: %d pre + %d post samples (%.0f ms latency)\n', ...
    PRE, POST, (PEAK_SEARCH + POST) / FS * 1000);

[X, valid] = buildFeatureMatrix(D, PRE, POST, FS, REMOVE_GRAVITY);
X = X(:, featMask);
trainIdx = find(isTrain & valid);
testIdx  = find(isTest & valid);
Xtr = X(trainIdx, :);  ytr = D.label(trainIdx);
gtr = cvGroups(valid(isTrain));

fprintf('\nLambda selection\n');
lamAcc = zeros(size(LAMBDA_LIST));
lamLoss = zeros(size(LAMBDA_LIST));
for i = 1:numel(LAMBDA_LIST)
    [yp, P] = crossValidateSoftmax(Xtr, ytr, gtr, LAMBDA_LIST(i));
    lamAcc(i) = 100 * mean(yp == ytr);
    lamLoss(i) = -mean(log(P(sub2ind(size(P), (1:numel(ytr))', ytr + 1)) + 1e-12));
    fprintf('  lambda %-8g  accuracy %6.2f %%   log-loss %.3f\n', ...
        LAMBDA_LIST(i), lamAcc(i), lamLoss(i));
end
[~, k] = min(lamLoss);
LAMBDA = LAMBDA_LIST(k);
fprintf('Selected lambda = %g\n', LAMBDA);

[ycv, Pcv] = crossValidateSoftmax(Xtr, ytr, gtr, LAMBDA);
CV = clashMetrics(ytr, ycv, Pcv);
printMetrics(sprintf('CROSS-VALIDATION (%s)', cvName), CV, zoneNames);

fig = figure('Name', 'CV confusion', 'Position', [100 100 560 460]);
cc = confusionchart(categorical(ytr, 0:2, zoneNames), categorical(ycv, 0:2, zoneNames));
cc.FontSize = 16;
title(sprintf('Cross-validation (%s): %.1f %%', cvName, CV.accuracy));
saveFig(fig, RESULTS, 'fig_confusion_cv');

model = trainSoftmax(Xtr, ytr, LAMBDA);
fprintf('\nFinal model trained on %d clashes (%d iterations)\n', ...
    numel(ytr), model.iterations);

TEST = [];
if ~isempty(testIdx)
    Xte = X(testIdx, :);  yte = D.label(testIdx);
    [yhat, Pte] = predictSoftmax(model, Xte);
    TEST = clashMetrics(yte, yhat, Pte);
    printMetrics('INDEPENDENT TEST (held-out session)', TEST, zoneNames);

    for ts = unique(D.session(testIdx))'
        m = D.session(testIdx) == ts;
        fprintf('  %s: %.1f %% (%d/%d)\n', ts, 100*mean(yhat(m) == yte(m)), ...
                sum(yhat(m) == yte(m)), sum(m));
    end

    fig = figure('Name', 'Test confusion', 'Position', [100 100 560 460]);
    cc = confusionchart(categorical(yte, 0:2, zoneNames), categorical(yhat, 0:2, zoneNames));
    cc.FontSize = 16;
    title(sprintf('Independent test: %.1f %%', TEST.accuracy));
    saveFig(fig, RESULTS, 'fig_confusion_test');

    conf = max(Pte, [], 2);
    fig = figure('Name', 'Confidence'); hold on;
    histogram(conf(yhat == yte), 0.3:0.05:1, 'DisplayName', 'correct');
    histogram(conf(yhat ~= yte), 0.3:0.05:1, 'DisplayName', 'incorrect');
    xlabel('Highest class probability'); ylabel('Clashes'); legend; grid on;
    title('Prediction confidence on the independent test');
    saveFig(fig, RESULTS, 'fig_confidence_test');

    writetable(table(D.file(testIdx), D.session(testIdx), yte, yhat, ...
        Pte(:,1), Pte(:,2), Pte(:,3), 'VariableNames', ...
        {'File','Session','Actual','Predicted','P_Bottom','P_Middle','P_Top'}), ...
        fullfile(RESULTS, 'test_predictions.csv'));
end

usedNames = featNames(featMask);
importance = sqrt(sum(model.W.^2, 1));
[~, order] = sort(importance, 'descend');
top = order(1:15);
fig = figure('Name', 'Feature importance', 'Position', [100 100 700 450]);
barh(importance(fliplr(top)));
yticks(1:15); yticklabels(usedNames(fliplr(top)));
xlabel('||w|| across classes (standardised features)');
title('Most influential features'); grid on;
saveFig(fig, RESULTS, 'fig_feature_importance');
fprintf('\nTop 10 features: %s\n', strjoin(usedNames(order(1:10)), ', '));

info = sprintf('Trained on %d clashes (%s), CV %.1f %%', numel(ytr), ...
    strjoin(trainSessions, ','), CV.accuracy);
if ~isempty(TEST), info = sprintf('%s, test %.1f %%', info, TEST.accuracy); end
fullModel = model;
fullModel.W = zeros(size(model.W, 1), numel(featMask));
fullModel.W(:, featMask) = model.W;
fullModel.mu = zeros(1, numel(featMask));    fullModel.mu(featMask) = model.mu;
fullModel.sigma = ones(1, numel(featMask));  fullModel.sigma(featMask) = model.sigma;
exportClashModel(fullModel, PRE, POST, FS, fullfile(RESULTS, 'ClashModel.h'), info, REMOVE_GRAVITY);

save(fullfile(RESULTS, 'clash_results_v2.mat'), 'model', 'PRE', 'POST', 'LAMBDA', ...
    'CV', 'TEST', 'sweepAcc', 'PRE_LIST', 'POST_LIST', 'LAMBDA_LIST', 'lamAcc', ...
    'lamLoss', 'importance', 'featNames', 'featMask', 'EXCLUDE_FEATURES', 'trainSessions', 'TEST_SESSIONS');

fprintf('\n============================================\n');
fprintf('SUMMARY FOR THE REPORT\n');
fprintf('============================================\n');
fprintf('Clashes (train / test)     : %d / %d\n', numel(ytr), numel(testIdx));
fprintf('Window                     : %d + %d samples = %.0f ms\n', PRE, POST, (PRE+POST)/FS*1000);
fprintf('Classification latency     : %.0f ms after the |a| peak\n', (PEAK_SEARCH+POST)/FS*1000);
fprintf('Lambda                     : %g\n', LAMBDA);
fprintf('Gravity removed            : %d\n', REMOVE_GRAVITY);
fprintf('CV accuracy (%s): %.1f %% [%.1f, %.1f]\n', cvName, CV.accuracy, CV.accuracyCI);
if ~isempty(TEST)
    fprintf('Independent test accuracy  : %.1f %% [%.1f, %.1f]\n', TEST.accuracy, TEST.accuracyCI);
    fprintf('Independent test macro F1  : %.3f\n', TEST.macroF1);
end
fprintf('Copy results_v2/ClashModel.h into firmware/Lightsaber_Main/\n');

function printMetrics(titleText, R, zoneNames)
fprintf('\n============================================\n%s\n', titleText);
fprintf('============================================\n');
fprintf('Accuracy  %.2f %%  (95%% CI %.1f-%.1f %%, n = %d)\n', R.accuracy, R.accuracyCI, R.n);
fprintf('Macro F1  %.3f\n\n', R.macroF1);
fprintf('                 Predicted\n');
fprintf('                 Bottom  Middle     Top\n');
for c = 1:3
    fprintf('Actual %-8s %7d %7d %7d\n', zoneNames{c}, R.confusion(c, :));
end
fprintf('\n%-8s %9s %9s %9s %9s\n', 'Zone', 'Precision', 'Recall', 'F1', 'AUC');
for c = 1:3
    fprintf('%-8s %9.3f %9.3f %9.3f %9.3f\n', zoneNames{c}, R.precision(c), ...
        R.recall(c), R.f1(c), R.auc(c));
end
end

function saveFig(fig, folder, name)
exportgraphics(fig, fullfile(folder, [name '.png']), 'Resolution', 300);
exportgraphics(fig, fullfile(folder, [name '.emf']), 'ContentType', 'vector');
exportgraphics(fig, fullfile(folder, [name '.pdf']), 'ContentType', 'vector');
end
