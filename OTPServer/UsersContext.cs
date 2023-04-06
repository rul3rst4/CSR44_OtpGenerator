using Microsoft.EntityFrameworkCore;
using Microsoft.Extensions.Hosting;
using System.Reflection.Metadata;

namespace OTPServer;

public class UsersContext : DbContext
{
    public DbSet<User> Users { get; set; }

    public string DbPath { get; }

    public UsersContext(DbContextOptions<UsersContext> options)
    {
        DbPath = "./OTPUsers.db";
    }

    // The following configures EF to create a Sqlite database file in the
    // special "local" folder for your platform.
    protected override void OnConfiguring(DbContextOptionsBuilder options)
        => options.UseSqlite($"Data Source={DbPath}");
}

public class User
{
    public int Id { get; set; }
    public string? Username { get; set; }
    public string? SeedPassword { get; set; }
    public string? Salt { get; set; }
}

public class Otp
{
    public string? Username { get; set; }
    public string? OtpString { get; set; }
}

public class DateTimeApi
{
    public DateTimeOffset? datetime { get; set; }
}
