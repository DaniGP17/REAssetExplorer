using FluentAssertions;
using REAssetExplorer.Core.Assets;
using REAssetExplorer.Core.Pak;
using REAssetExplorer.Testing.Fixtures;
using Xunit;

namespace REAssetExplorer.Testing.TestHelpers;

public class AssetReaderTestHelper
{
    public record TestResult(
        string FilePath,
        bool Success,
        string? Error,
        long Size,
        TimeSpan Duration
    );

    public class TestSummary
    {
        public List<TestResult> Results { get; } = new();
        public int TotalFiles => Results.Count;
        public int SuccessfulFiles => Results.Count(r => r.Success);
        public int FailedFiles => Results.Count(r => !r.Success);
        public double SuccessRate => TotalFiles > 0 ? SuccessfulFiles * 100.0 / TotalFiles : 0;
        
        public void PrintReport(string assetType)
        {
            Console.WriteLine($"\n=== {assetType} Load Test Results ===");
            Console.WriteLine($"Total files: {TotalFiles}");
            Console.WriteLine($"Successful: {SuccessfulFiles} ({SuccessRate:F1}%)");
            Console.WriteLine($"Failed: {FailedFiles} ({FailedFiles * 100.0 / TotalFiles:F1}%)");
            
            if (FailedFiles > 0)
            {
                Console.WriteLine($"\n=== Failed Files ===");
                foreach (var result in Results.Where(r => !r.Success))
                {
                    Console.WriteLine($" {result.FilePath}");
                    Console.WriteLine($"   Size: {result.Size:N0} bytes");
                    Console.WriteLine($"   Duration: {result.Duration.TotalMilliseconds:F0}ms");
                    Console.WriteLine($"   Error: {TruncateError(result.Error)}");
                }
            }
            
            Console.WriteLine($"\n=== Sample Successful Files ===");
            foreach (var result in Results.Where(r => r.Success).Take(5))
            {
                Console.WriteLine($" {result.FilePath} ({result.Size:N0} bytes, {result.Duration.TotalMilliseconds:F0}ms)");
            }
            
            var successfulSizes = Results.Where(r => r.Success).Select(r => r.Size).ToList();
            if (successfulSizes.Any())
            {
                Console.WriteLine($"\n=== Size Statistics (Successful) ===");
                Console.WriteLine($"Min: {successfulSizes.Min():N0} bytes");
                Console.WriteLine($"Max: {successfulSizes.Max():N0} bytes");
                Console.WriteLine($"Avg: {successfulSizes.Average():N0} bytes");
            }
            
            var durations = Results.Where(r => r.Success).Select(r => r.Duration.TotalMilliseconds).ToList();
            if (durations.Any())
            {
                Console.WriteLine($"\n=== Performance Statistics ===");
                Console.WriteLine($"Min: {durations.Min():F2}ms");
                Console.WriteLine($"Max: {durations.Max():F2}ms");
                Console.WriteLine($"Avg: {durations.Average():F2}ms");
            }
            
            var errorGroups = Results
                .Where(r => !r.Success)
                .GroupBy(r => ExtractErrorType(r.Error))
                .OrderByDescending(g => g.Count());
            
            if (errorGroups.Any())
            {
                Console.WriteLine($"\n=== Error Types Summary ===");
                foreach (var group in errorGroups)
                {
                    Console.WriteLine($"{group.Key}: {group.Count()} files");
                }
            }
            
            Console.WriteLine($"\n Test completed. {SuccessfulFiles}/{TotalFiles} files successfully parsed.");
        }
        
        public static string TruncateError(string? error, int maxLength = 200)
        {
            if (string.IsNullOrEmpty(error))
                return "Unknown error";
            
            if (error.Length <= maxLength)
                return error;
            
            return error.Substring(0, maxLength) + "...";
        }
        
        public static string ExtractErrorType(string? error)
        {
            if (string.IsNullOrEmpty(error))
                return "Unknown";
            
            if (error.Contains("NotSupportedException"))
                return "NotSupportedException";
            if (error.Contains("ArgumentException"))
                return "ArgumentException";
            if (error.Contains("InvalidDataException"))
                return "InvalidDataException";
            
            var firstLine = error.Split('\n')[0];
            return firstLine.Length > 50 ? firstLine.Substring(0, 50) + "..." : firstLine;
        }
    }
    
    /// <summary>
    /// Matches a file path against a pattern that supports wildcards.
    /// </summary>
    private static bool MatchesPattern(string? filePath, string pattern)
    {
        if (string.IsNullOrEmpty(filePath))
            return false;
        
        if (!pattern.Contains('*'))
        {
            return filePath.EndsWith(pattern, StringComparison.OrdinalIgnoreCase);
        }

        var regexPattern = System.Text.RegularExpressions.Regex.Escape(pattern)
            .Replace("\\*", ".*");
        
        regexPattern = regexPattern + "$";
        
        return System.Text.RegularExpressions.Regex.IsMatch(
            filePath, 
            regexPattern, 
            System.Text.RegularExpressions.RegexOptions.IgnoreCase);
    }
    
    public static TestSummary TestAllAssetsOfType<TAssetReader, TAssetData>(
        PakFileFixture fixture,
        PakFile pakFile,
        string fileExtension,
        TAssetReader reader,
        int? maxFiles = null)
        where TAssetReader : IAssetReader<TAssetData>
        where TAssetData : class
    {
        var summary = new TestSummary();
        
        var entries = pakFile.Entries
            .Where(e => MatchesPattern(e.FilePath, fileExtension))
            .ToList();
        
        Console.WriteLine($"\n=== Asset Loading Debug Info ===");
        Console.WriteLine($"Total entries in PAK: {pakFile.Entries.Count()}");
        Console.WriteLine($"Entries matching pattern '{fileExtension}': {entries.Count}");
        
        if (entries.Count == 0)
        {
            var samplePaths = pakFile.Entries
                .Where(e => !string.IsNullOrEmpty(e.FilePath))
                .Select(e => e.FilePath)
                .Take(20)
                .ToList();
            
            Console.WriteLine($"Sample file paths from PAK:");
            foreach (var path in samplePaths)
            {
                Console.WriteLine($"  {path}");
            }
            
            var baseExtension = fileExtension.Contains('*') 
                ? fileExtension.Split('*')[0].Split('.').FirstOrDefault(s => s.Length > 0)
                : fileExtension.Split('.').FirstOrDefault(s => s.Length > 0);
            
            if (!string.IsNullOrEmpty(baseExtension))
            {
                var similarExtensions = pakFile.Entries
                    .Where(e => e.FilePath?.Contains(baseExtension, StringComparison.OrdinalIgnoreCase) == true)
                    .Select(e => e.FilePath)
                    .Take(10)
                    .ToList();
                
                if (similarExtensions.Any())
                {
                    Console.WriteLine($"\nFiles containing '{baseExtension}':");
                    foreach (var path in similarExtensions)
                    {
                        Console.WriteLine($"  {path}");
                    }
                }
            }
        }
        
        if (maxFiles.HasValue)
        {
            entries = entries.Take(maxFiles.Value).ToList();
        }
        
        foreach (var entry in entries)
        {
            var stopwatch = System.Diagnostics.Stopwatch.StartNew();
            
            var data = fixture.ExtractFile(pakFile, entry);
            
            if (data == null || data.Length == 0)
            {
                summary.Results.Add(new TestResult(
                    entry.FilePath,
                    false,
                    "Failed to extract from PAK",
                    0,
                    stopwatch.Elapsed
                ));
                continue;
            }
            
            var result = reader.Read(data, entry.FilePath);
            stopwatch.Stop();
            
            summary.Results.Add(new TestResult(
                entry.FilePath,
                result.IsSuccess,
                result.IsSuccess ? null : result.Error,
                data.Length,
                stopwatch.Elapsed
            ));
        }
        
        return summary;
    }
    
    public static void AssertMinimumSuccessRate(
        TestSummary summary,
        double minimumSuccessRate,
        string assetType)
    {
        summary.TotalFiles.Should().BeGreaterThan(0,
            $"No {assetType} files were found in the PAK. Check if the file extension filter is correct and if the PAK file was loaded properly.");
        
        summary.SuccessfulFiles.Should().BeGreaterThan(0,
            $"At least some {assetType} files should be parseable. Total files found: {summary.TotalFiles}, Failed: {summary.FailedFiles}");
        
        if (minimumSuccessRate > 0)
        {
            var failureDetails = "";
            if (summary.SuccessRate < minimumSuccessRate)
            {
                failureDetails = BuildFailureDetails(summary);
            }
            
            summary.SuccessRate.Should().BeGreaterThanOrEqualTo(minimumSuccessRate,
                $"Expected at least {minimumSuccessRate:F1}% success rate for {assetType}, but got {summary.SuccessRate:F1}%{failureDetails}");
        }
    }
    
    private static string BuildFailureDetails(TestSummary summary)
    {
        var details = new System.Text.StringBuilder();
        details.AppendLine($"\n\n=== Failed Files ({summary.FailedFiles}/{summary.TotalFiles}) ===");
        
        var errorGroups = summary.Results
            .Where(r => !r.Success)
            .GroupBy(r => TestSummary.ExtractErrorType(r.Error))
            .OrderByDescending(g => g.Count());
        
        foreach (var group in errorGroups)
        {
            details.AppendLine($"\n{group.Key} ({group.Count()} files):");
            foreach (var result in group.Take(5))
            {
                var fileName = System.IO.Path.GetFileName(result.FilePath);
                var errorMsg = result.Error ?? "Unknown error";
                
                details.AppendLine($"  • {fileName}");
                
                var errorLines = errorMsg.Split(new[] { '\r', '\n' }, StringSplitOptions.RemoveEmptyEntries);
                var mainError = errorLines.FirstOrDefault() ?? errorMsg;
                
                details.AppendLine($"    {mainError}");
            }
            if (group.Count() > 5)
            {
                details.AppendLine($"  ... and {group.Count() - 5} more");
            }
        }
        
        return details.ToString();
    }
}
