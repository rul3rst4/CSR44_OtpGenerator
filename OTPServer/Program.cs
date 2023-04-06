using Microsoft.EntityFrameworkCore;
using QRCoder;
using System.Globalization;
using System.Linq;
using System.Security.Cryptography;
using System.Text;
using System.Text.Json;
using System.Text.Json.Serialization;

namespace OTPServer;

public class TrueRandom
{

    public static int Next()
    {
        // Generate a random number using a cryptographically secure random number generator
        using (var rng = new System.Security.Cryptography.RNGCryptoServiceProvider())
        {
            var randomNumber = new byte[4];
            rng.GetBytes(randomNumber);
            return BitConverter.ToInt32(randomNumber, 0);
        }
    }
}
public class Program
{
    public static void Main(string[] args)
    {
        var builder = WebApplication.CreateBuilder(args);

        // Add services to the container.
        builder.Services.AddAuthorization();

        // Learn more about configuring Swagger/OpenAPI at https://aka.ms/aspnetcore/swashbuckle
        builder.Services.AddEndpointsApiExplorer();
        builder.Services.AddSwaggerGen();
        builder.Services.AddHttpClient();
        builder.Services.AddCors();
        builder.Services.AddDbContext<UsersContext>(opt => opt.UseSqlite());

        var app = builder.Build();

        // Configure the HTTP request pipeline.
        if (app.Environment.IsDevelopment())
        {
            app.UseSwagger();
            app.UseSwaggerUI();
        }

        app.UseHttpsRedirection();
        app.UseCors();
        app.UseAuthorization();

        var summaries = new[]
        {
            "Freezing", "Bracing", "Chilly", "Cool", "Mild", "Warm", "Balmy", "Hot", "Sweltering", "Scorching"
        };

        app.MapPost("/GetSalt", async (User user, UsersContext ctx) => {
            if (string.IsNullOrEmpty(user.Username))
            {
                return Results.BadRequest();
            }

            var randomSalt = BitConverter.ToString(RandomNumberGenerator.GetBytes(6));
            QRCodeGenerator qrGenerator = new QRCodeGenerator();
            QRCodeData qrCodeData = qrGenerator.CreateQrCode(randomSalt, QRCodeGenerator.ECCLevel.Q);
            PngByteQRCode qrCode = new PngByteQRCode(qrCodeData);
            byte[] qrCodeAsPngByteArr = qrCode.GetGraphic(20);

            var existingUser = await ctx.Users.Where(e => e.Username == user.Username).FirstOrDefaultAsync();
            if (existingUser is not null && !string.IsNullOrEmpty(existingUser.SeedPassword))
            {
                return Results.Unauthorized();
            }

            var salt = Convert.ToBase64String(SHA256.HashData(System.Text.Encoding.UTF8.GetBytes(randomSalt)));

            if (existingUser is null)
            {
                user.Salt = salt;
                user.SeedPassword = string.Empty;
                ctx.Users.Add(user);
            }
            else
            {
                existingUser.Salt = salt;
                ctx.Users.Update(existingUser);
            }

            await ctx.SaveChangesAsync();
            // create salt for the user. Associate with the username.
            return Results.Ok(new { randomSalt = qrCodeAsPngByteArr });
        })
        .WithName("GetSalt");

        app.MapPost("/CreateUser", async (User user, UsersContext ctx) => {
            var newUser = await ctx.Users.Where(e => e.Username == user.Username && e.Salt == user.Salt).FirstOrDefaultAsync();

            if (newUser is null || !string.IsNullOrEmpty(newUser.SeedPassword))
            {
                return Results.Unauthorized();
            }

            if (string.IsNullOrEmpty(user.SeedPassword)) { return Results.BadRequest(); }

            newUser.SeedPassword = user.SeedPassword;
            ctx.Users.Update(newUser); 
            await ctx.SaveChangesAsync();

            return Results.Created("user", user.Username);
        })
        .WithName("CreateUser");

        app.MapPost("/Login", async (User user, UsersContext ctx) => {
            var newUser = await ctx.Users.Where(e => e.Username == user.Username && e.SeedPassword == user.SeedPassword).FirstOrDefaultAsync();

            if (newUser is null || string.IsNullOrEmpty(newUser.SeedPassword))
            {
                return Results.Unauthorized();
            }

            return Results.Ok();
        })
        .WithName("Login");

        app.MapPost("/GetSecuredContent", async (Otp otp, UsersContext ctx, IHttpClientFactory httpClientFactory) => {
            var user = await ctx.Users.Where(e => e.Username == otp.Username).FirstOrDefaultAsync();

            if (user is null || string.IsNullOrEmpty(user.SeedPassword) || string.IsNullOrEmpty(user.Salt))
            {
                return Results.Unauthorized();
            }

            using var httpClient = new HttpClient();

            using var result = await httpClient.GetAsync("http://worldtimeapi.org/api/timezone/America/Sao_Paulo", HttpCompletionOption.ResponseContentRead);

            var data = JsonSerializer.Deserialize<DateTimeApi>(await result.Content.ReadAsStringAsync());

            var offset = data!.datetime;
            for (int i=0; i<60; i++)
            {
                var generated_otp = GenerateOtp(Convert.FromBase64String(user.Salt), Convert.FromBase64String(user.SeedPassword), offset.Value);

                if (otp.OtpString == generated_otp)
                {
                    return Results.Ok();
                }

                offset = offset.Value.Subtract(TimeSpan.FromSeconds(1));
            }


            return Results.Unauthorized();
        })
        .WithName("GetSecuredContent");

        app.Run();
    }

    public static string GenerateOtp(byte[] salt, byte[] seedPassword, DateTimeOffset time)
    {
        byte[] hash;
        string a;
        using (SHA256 sha256 = SHA256.Create())
        {

            string dateTime = new StringBuilder(time.Year.ToString())
                .Append(time.Month.ToString())
                .Append(time.Day.ToString())
                .Append(time.Hour.ToString())
                .Append(time.Minute.ToString())
                .Append(time.Second.ToString()).ToString();

            List<byte> inputData = new List<byte>();
            inputData.AddRange(salt);
            inputData.AddRange(seedPassword);
            inputData.AddRange(Encoding.ASCII.GetBytes(dateTime));

            hash = sha256.ComputeHash(inputData.ToArray());
        }

        StringBuilder otp = new StringBuilder();
        foreach (byte b in hash)
        {
            otp.Append(b.ToString());
        }

        otp.Length = 6;

        return otp.ToString();
    }
}