%% CAPTURECLASHES  Record clashes from ClashLogger over USB into data/clash_dataset/<SESSION>.
clear; clc;

PORT     = "COM5";        % <-- check Device Manager > Ports
BAUD     = 115200;
SESSION  = "V03";         % horizontal: S01-S03 train, S04 test | vertical: V01-V02 train, V03 test
dataRoot = fullfile(fileparts(mfilename('fullpath')), '..', 'data', 'clash_dataset');

outFolder = fullfile(dataRoot, SESSION);
if ~exist(outFolder, 'dir'), mkdir(outFolder); end
logFile = fullfile(outFolder, 'clash_log.csv');

if isfile(logFile)
    opts = detectImportOptions(logFile);
    opts = setvartype(opts, {'zone','strength','face','file','time'}, 'string');
    LOG = readtable(logFile, opts);
    fprintf('Continuing session %s (%d clashes already saved)\n', SESSION, height(LOG));
else
    LOG = table('Size', [0 11], ...
        'VariableTypes', ["double","string","string","string","double","double", ...
                          "double","double","double","string","string"], ...
        'VariableNames', ["id","zone","strength","face","peak_g","jerk_g", ...
                          "pre_gyro_dps","fs_hz","overflows","file","time"]);
end

sp = serialport(PORT, BAUD);
configureTerminator(sp, "CR/LF");
sp.Timeout = 60;                 % wait up to 60 s for the next line
flush(sp);
cleanupObj = onCleanup(@() clear('sp'));
pause(0.5);
write(sp, '?', 'char');   % ask the logger to repeat the current prompt

fprintf('\nCapturing session %s into %s\nCtrl+C to stop.\n\n', SESSION, outFolder);
zoneNames = ["BOTTOM", "MIDDLE", "TOP"];
warnedMain = false;
idOffset = 0;
if height(LOG) > 0, idOffset = max(LOG.id); end   % keep ids unique per session

while true
    line = readline(sp);
    if isempty(line), continue; end      % no clash yet (timeout) - keep waiting
    line = strtrim(line);
    if strlength(line) == 0, continue; end

    if ~startsWith(line, "#") && ~warnedMain
        fprintf(2, ['\nThe lightsabre is sending normal debug output, not logger data.\n' ...
                    'Upload kit\\firmware\\ClashLogger\\ClashLogger.ino, close the Serial Monitor,\n' ...
                    'then run "clear" and this script again.\n\n']);
        warnedMain = true;
    end

    if startsWith(line, "#CLASH")
        p = split(line, ",");
        id = str2double(p(2)) + idOffset;
        rows = zeros(256, 7);
        for r = 1:256
            rows(r, :) = str2double(split(strtrim(readline(sp)), ","))';
        end
        readline(sp);   % #END

        name = sprintf('%s_%s_%04d.csv', p(3), SESSION, id);
        T = array2table(rows, 'VariableNames', ...
            {'sample','ax_g','ay_g','az_g','gx_dps','gy_dps','gz_dps'});
        writetable(T, fullfile(outFolder, name));

        LOG(end+1, :) = {id, p(3), p(4), p(5), str2double(p(6)), str2double(p(7)), ...
            str2double(p(8)), str2double(p(9)), str2double(p(10)), string(name), ...
            string(datetime('now', 'Format', 'HH:mm:ss'))};
        writetable(LOG, logFile);

        fs = str2double(p(9));
        warn = "";
        if fs < 490 || fs > 510, warn = "   <-- SAMPLE RATE WRONG, stop and check"; end
        counts = arrayfun(@(z) sum(LOG.zone == z), zoneNames);
        fprintf('  saved #%-4d %-6s %-6s %-5s  peak %5.1f g   %5.1f Hz   [B %d | M %d | T %d]%s\n', ...
            id, p(3), p(4), p(5), str2double(p(6)), fs, counts, warn);

    elseif startsWith(line, "#REJECT")
        id = str2double(extractAfter(line, "#REJECT,")) + idOffset;
        k = find(LOG.id == id, 1);
        if ~isempty(k)
            delete(fullfile(outFolder, LOG.file(k)));
            LOG(k, :) = [];
            writetable(LOG, logFile);
            fprintf('  REJECTED #%d - repeat the same strike\n', id);
        end

    elseif startsWith(line, "#NEXT")
        p = split(extractAfter(line, "#NEXT,"), ",");
        fprintf('\nNEXT  ->  %-6s  %-6s  %-5s   (%s)\n', p(1), p(2), p(3), p(4));

    elseif startsWith(line, "#BLOCK")
        fprintf('\n===== BLOCK %s =====\n', extractAfter(line, "#BLOCK,"));

    elseif startsWith(line, "#")
        fprintf('%s\n', line);
    end
end
