% Original PPG data
ppgData = [518, 585, 891, 754, 471, 249, 306, 411, 569, 555, 523, 456, 475, ...
           469, 518, 494, 523, 513, 769, 884, 638, 331, 264, 348, 519, 573, ...
           533, 476, 462, 475, 496, 519, 518, 528, 588, 825, 717, 470, 305, ...
           355, 427, 547, 535, 540, 475, 510, 483, 533, 506, 550, 570, 800, ...
           693, 505, 319, 374, 436, 573, 537, 536, 457, 504, 469, 519, 471, ...
           533, 738, 886, 559, 336, 224, 378, 488, 594, 507, 488, 428, 483, ...
           470, 532, 497, 613, 925, 926, 554, 303, 228, 394, 535, 603, 511, ...
           481, 437, 483, 483, 514, 481, 508, 733, 926, 674, 350, 220, 332, ...
           498, 590, 549, 504, 474, 485, 513, 533, 541, 526, 545, 761, 914, ...
           613, 355, 278, 409, 519, 605, 543, 524, 474, 524, 509, 561, 525, ...
           558, 530, 804, 858, 618, 331, 336, 396, 565, 559, 579, 500, 532, ...
           487, 542, 506, 558, 522, 715, 853, 703, 379, 336, 369, 537, 561, ...
           580, 502, 525, 498, 547, 518, 549, 516, 713, 906, 730, 384, 303, ...
           348, 523, 577, 586, 511, 506, 488, 536, 534, 556, 533, 643, 926, ...
           883, 493, 265, 283, 451, 591, 580, 523, 466, 493, 495, 541, 524, ...
           552, 535, 848, 926, 738, 340, 257, 325, 537, 579, 579, 480, 504, ...
           474, 540, 510, 560, 514, 601, 820, 855, 483, 315, 276, 446, 509, ...
           570, 483, 502, 449, 487, 426];

ppgData = -ppgData + max(ppgData);

% Clear conflicting variables
clear sum

% Define filter window size
FILTER_WINDOW = 9;  % Can be adjusted to other odd values

% Check if FILTER_WINDOW is valid
if FILTER_WINDOW <= 0 || mod(FILTER_WINDOW, 2) == 0
    error('FILTER_WINDOW must be an odd number greater than 0.');
end

HALF_WINDOW = floor(FILTER_WINDOW / 2);
filteredData = zeros(size(ppgData));

% Calculate weights (triangular window)
weights = zeros(1, FILTER_WINDOW);
for i = 1:FILTER_WINDOW
    weights(i) = HALF_WINDOW + 1 - abs(i - (HALF_WINDOW + 1));
end

% Debug output
disp(['FILTER_WINDOW: ', num2str(FILTER_WINDOW)]);
disp(['weights: ', num2str(weights)]);

if length(weights) ~= FILTER_WINDOW
    error('Weight array dimension mismatch! Expected length %d, actual length %d', FILTER_WINDOW, length(weights));
end

% Check weight sum before normalization
weightSum = sum(weights); % Use full function name
if weightSum == 0
    error('Weight coefficient sum is 0, please check window parameters');
end
weights = weights / weightSum; % Replace original line 49 operation

clear sum % Add cleanup statement at the beginning of the code

% Apply moving average with triangular window
for i = 1:length(ppgData)
    windowSum = 0;  % Avoid using sum as a variable name
    validWeights = 0;
    
    for j = -HALF_WINDOW:HALF_WINDOW
        idx = i + j;
        % Improved boundary handling
        if idx < 1
            idx = 1;
        elseif idx > length(ppgData)
            idx = length(ppgData);
        end
        
        weightIdx = j + HALF_WINDOW + 1;
        if weightIdx > length(weights)  % New boundary check
            continue;
        end
        
        windowSum = windowSum + ppgData(idx) * weights(weightIdx);  % 修正变量名拼写错误
        validWeights = validWeights + weights(weightIdx);
    end
    
    % Weight compensation
    if validWeights > 0
        filteredData(i) = windowSum / validWeights;
    else
        filteredData(i) = ppgData(i);
    end
end


% Calculate second-order difference (APG signal) with normalization
apgData = zeros(size(filteredData));
for i = 3:length(filteredData)
    apgData(i) = filteredData(i) - 2 * filteredData(i-1) + filteredData(i-2);
end

% Normalize APG signal
maxAPG = max(apgData);
minAPG = min(apgData);

if maxAPG ~= minAPG
    apgData = (apgData - minAPG) / (maxAPG - minAPG);
end

% Add visualization of APG signal
figure;
subplot(2,1,1);
plot(filteredData);
title('Filtered PPG Signal');
xlabel('Sample');
ylabel('Amplitude');
grid on;

subplot(2,1,2);
plot(apgData);
title('Normalized APG Signal');
xlabel('Sample');
ylabel('Normalized Amplitude');
grid on;

% Set threshold
threshold = 0.6 * max(apgData);

% Detect A points
aPoints = [];
for i = 2:length(apgData)-1
    if apgData(i) > threshold && apgData(i) > apgData(i-1) && apgData(i) > apgData(i+1)
        aPoints = [aPoints, i];
    end
end

% Main peak (B point) detection
[peakValues, bPoints] = findpeaks(filteredData, 'MinPeakHeight', mean(filteredData), ...
    'MinPeakDistance', 15, 'MinPeakProminence', 50);

% Main trough (A point) detection
aPoints = [];
for i = 1:length(bPoints)
    % Search for the closest valley before each B point
    if i == 1
        searchRange = 1:bPoints(i);
    else
        searchRange = bPoints(i-1):bPoints(i);
    end
    if length(searchRange) > 1
        [~, valleys] = findpeaks(-filteredData(searchRange));
        if ~isempty(valleys)
            % Select the valley closest to the B point
            [~, idx] = min(abs(valleys - length(searchRange)));
            aPoints = [aPoints, searchRange(1) + valleys(idx) - 1];
        end
    end
end

% Secondary trough (C point) detection
cPoints = [];
for i = 1:length(bPoints)
    if i < length(bPoints)
        searchEnd = bPoints(i+1);
    else
        searchEnd = length(filteredData);
    end
    searchRange = bPoints(i):searchEnd;
    
    if length(searchRange) > 1
        [~, valleys] = findpeaks(-filteredData(searchRange), 'MinPeakDistance', 5);
        if ~isempty(valleys)
            % Select the first valley after the B point
            cPoints = [cPoints, searchRange(1) + valleys(1) - 1];
        end
    end
end

% Secondary peak (D point) detection
dPoints = [];
for i = 1:length(cPoints)
    if i < length(bPoints)
        searchEnd = bPoints(i+1);
    else
        searchEnd = length(filteredData);
    end
    searchRange = cPoints(i):searchEnd;
    
    if length(searchRange) > 1
        [~, peaks] = findpeaks(filteredData(searchRange), 'MinPeakDistance', 5);
        if ~isempty(peaks)
            % Select the first peak after the C point
            dPoints = [dPoints, searchRange(1) + peaks(1) - 1];
        end
    end
end

% Output correction
disp('Detected feature points:');
disp(['A points (main troughs): ', num2str(aPoints)]);
disp(['B points (main peaks): ', num2str(bPoints)]);
disp(['C points (secondary troughs): ', num2str(cPoints)]);
disp(['D points (secondary peaks): ', num2str(dPoints)]);

% Validate feature points before plotting
validAPoints = aPoints;
validBPoints = bPoints;
validCPoints = cPoints;
validDPoints = dPoints;

% Validate feature points
% Remove points out of range
validAPoints = validAPoints(validAPoints > 0 & validAPoints <= length(filteredData));
validBPoints = validBPoints(validBPoints > 0 & validBPoints <= length(filteredData));
validCPoints = validCPoints(validCPoints > 0 & validCPoints <= length(filteredData));
validDPoints = validDPoints(validDPoints > 0 & validDPoints <= length(filteredData));

% Ensure all points have reasonable amplitude
minValue = min(filteredData);
maxValue = max(filteredData);
validAPoints = validAPoints(filteredData(validAPoints) >= minValue & filteredData(validAPoints) <= maxValue);
validBPoints = validBPoints(filteredData(validBPoints) >= minValue & filteredData(validBPoints) <= maxValue);
validCPoints = validCPoints(filteredData(validCPoints) >= minValue & filteredData(validCPoints) <= maxValue);
validDPoints = validDPoints(filteredData(validDPoints) >= minValue & filteredData(validDPoints) <= maxValue);

% Plot filtered waveform and annotate A, B, C, D points
figure;
plot(ppgData, '-o', 'DisplayName', 'Original waveform');
hold on;
plot(filteredData, '-x', 'DisplayName', 'Filtered waveform'); % Ensure filtered waveform is displayed
if ~isempty(validAPoints)
    plot(validAPoints, filteredData(validAPoints), 'mo', 'MarkerSize', 8, 'LineWidth', 2, 'DisplayName', 'A points'); % Annotate A points
end
if ~isempty(validBPoints)
    plot(validBPoints, filteredData(validBPoints), 'ro', 'MarkerSize', 10, 'LineWidth', 2, 'DisplayName', 'B points'); % Annotate B points
end
if ~isempty(validCPoints)
    plot(validCPoints, filteredData(validCPoints), 'go', 'MarkerSize', 8, 'LineWidth', 2, 'DisplayName', 'C points'); % Annotate C points
end
if ~isempty(validDPoints)
    plot(validDPoints, filteredData(validDPoints), 'bo', 'MarkerSize', 8, 'LineWidth', 2, 'DisplayName', 'D points'); % Annotate D points
end
title('Original PPG waveform with filtered waveform and feature points');
xlabel('Data points');
ylabel('Amplitude');
legend('show'); % Ensure legend is displayed
grid on;

% Output detected feature points
disp('Detected feature points:');
disp(['A points (main troughs): ', num2str(validAPoints)]);
disp(['B points (main peaks): ', num2str(validBPoints)]);
disp(['C points (secondary troughs): ', num2str(validCPoints)]);
disp(['D points (secondary peaks): ', num2str(validDPoints)]);