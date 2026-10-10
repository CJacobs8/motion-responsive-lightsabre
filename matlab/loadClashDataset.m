function D = loadClashDataset(rootFolder, opts)
% LOADCLASHDATASET  Load clash CSVs into aligned windows.

if nargin < 2, opts = struct(); end
if ~isfield(opts, 'pre'),        opts.pre = 64;          end
if ~isfield(opts, 'post'),       opts.post = 192;        end
if ~isfield(opts, 'triggerG'),   opts.triggerG = 3.0;    end   % = ccTriggerG
if ~isfield(opts, 'jerkG'),      opts.jerkG = 1.0;       end   % = ccTriggerJerk
if ~isfield(opts, 'peakSearch'), opts.peakSearch = 8;    end   % = CC_PEAK_SEARCH
if ~isfield(opts, 'excludeFolders'), opts.excludeFolders = {}; end

zoneNames = ["BOTTOM", "MIDDLE", "TOP"];
L = opts.pre + opts.post;

files = dir(fullfile(rootFolder, '**', '*.csv'));
keep = false(numel(files), 1);
for i = 1:numel(files)
    name = upper(string(files(i).name));
    isZone = arrayfun(@(z) startsWith(name, z + "_"), zoneNames);
    isFeat = endsWith(name, "_FEAT.CSV");
    [~, sub] = fileparts(files(i).folder);
    excluded = any(strcmpi(sub, opts.excludeFolders));
    keep(i) = any(isZone) && ~isFeat && ~excluded;
end
files = files(keep);

M = numel(files);
D.win     = nan(L, 6, M);
D.label   = zeros(M, 1);
D.session = strings(M, 1);
D.file    = strings(M, 1);
D.peakG   = nan(M, 1);
D.pre     = opts.pre;
D.post    = opts.post;
D.format  = strings(M, 1);
ok = true(M, 1);

rootFull = string(fullfile(rootFolder));

for i = 1:M
    path = fullfile(files(i).folder, files(i).name);
    data = readmatrix(path);
    data = data(all(isfinite(data(:, 1:7)), 2), 1:7);
    if size(data, 1) < 20
        ok(i) = false;
        warning('%s: too few rows, skipped', files(i).name);
        continue;
    end

    name = upper(string(files(i).name));
    zoneIdx = find(arrayfun(@(z) startsWith(name, z + "_"), zoneNames), 1);
    D.label(i) = zoneIdx - 1;
    D.file(i)  = string(files(i).name);

    folder = string(files(i).folder);
    if strcmpi(folder, rootFull)
        D.session(i) = "root";
    else
        [~, s] = fileparts(folder);
        D.session(i) = string(s);
    end

    sig = data(:, 2:7);
    amag = sqrt(sum(sig(:, 1:3).^2, 2));

    anchorRow = find(data(:, 1) == 0, 1);
    isNew = size(data, 1) == L && ~isempty(anchorRow) && data(1, 1) == -opts.pre;

    if isNew
        D.format(i) = "new";
        D.win(:, :, i) = sig;
        D.peakG(i) = amag(anchorRow);
    else
        D.format(i) = "old";
        jerk = [0; abs(diff(amag))];
        t = find(amag >= opts.triggerG & jerk >= opts.jerkG, 1);
        if isempty(t)
            [~, t] = max(amag);          % fallback: global peak
        end
        searchEnd = min(t + opts.peakSearch - 1, numel(amag));
        [D.peakG(i), k] = max(amag(t:searchEnd));
        anchor = t + k - 1;

        src = (anchor - opts.pre):(anchor + opts.post - 1);
        valid = src >= 1 & src <= size(sig, 1);
        D.win(valid, :, i) = sig(src(valid), :);
    end
end

D.win     = D.win(:, :, ok);
D.label   = D.label(ok);
D.session = D.session(ok);
D.file    = D.file(ok);
D.peakG   = D.peakG(ok);
D.format  = D.format(ok);

fprintf('Loaded %d clashes from %s\n', numel(D.label), rootFolder);
sessions = unique(D.session);
for s = sessions'
    m = D.session == s;
    fprintf('  session %-12s  bottom %3d  middle %3d  top %3d\n', s, ...
        sum(D.label(m) == 0), sum(D.label(m) == 1), sum(D.label(m) == 2));
end
end
