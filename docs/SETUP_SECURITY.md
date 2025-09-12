# Security Setup Guide

This document explains how to complete the security and quality setup for the `Elpablo777/tdesktop` repository.

## 🔧 Required GitHub Secrets

To enable all security features, the following secrets need to be configured in GitHub:

### 1. SonarCloud Integration

1. **Sign up for SonarCloud**:
   - Go to [SonarCloud.io](https://sonarcloud.io)
   - Sign up with your GitHub account (free for public repositories)

2. **Import the Repository**:
   - Click "+" → "Analyze new project"  
   - Select `Elpablo777/tdesktop`
   - Follow the setup wizard

3. **Get the SonarCloud Token**:
   - Go to [SonarCloud Account Security](https://sonarcloud.io/account/security)
   - Generate a new token with name `tdesktop-security`
   - Copy the token

4. **Add GitHub Secret**:
   - Go to repository Settings → Secrets and variables → Actions
   - Click "New repository secret"
   - Name: `SONAR_TOKEN`
   - Value: [paste the SonarCloud token]

### 2. Dependabot Alerts (Auto-enabled)

Dependabot should automatically work, but to ensure it's enabled:

1. Go to repository Settings → Security & analysis
2. Enable "Dependency graph" (usually auto-enabled for public repos)
3. Enable "Dependabot alerts" 
4. Enable "Dependabot security updates"

### 3. CodeQL Analysis (Auto-enabled)

CodeQL should work automatically, but to verify:

1. Go to repository Settings → Security & analysis  
2. Enable "Code scanning alerts"
3. The workflow will run automatically on the next push

## 🧪 Testing the Setup

After configuring the secrets, test the setup by:

1. **Trigger a manual workflow run**:
   - Go to Actions tab
   - Select "SonarCloud Analysis" 
   - Click "Run workflow" → "Run workflow"

2. **Check CodeQL**:
   - Push a small change to trigger CodeQL
   - Go to Security → Code scanning alerts

3. **Test upstream sync**:
   - Go to Actions tab
   - Select "Upstream Sync"
   - Click "Run workflow" → "Run workflow"

## 📊 Quality Gates

The SonarCloud quality gates are configured with:
- **New Bugs**: 0 (no new bugs allowed)
- **Coverage**: ≥ 50% (progressive goal)
- **Maintainability**: Rating B or better
- **Security**: No new vulnerabilities

## 🔍 Monitoring

After setup, regularly check:

- **Weekly**: Review CodeQL security alerts
- **Weekly**: Check SonarCloud quality gate status
- **Monthly**: Review Dependabot PRs and merge after testing
- **Monthly**: Check upstream sync status

## 🚨 Troubleshooting

### SonarCloud Analysis Fails
- Check if `SONAR_TOKEN` secret is correctly set
- Verify SonarCloud project is properly imported
- Check sonar-project.properties configuration

### CodeQL Analysis Fails
- Usually works automatically for C++ projects
- Check workflow logs for build errors
- May need adjustments for complex build systems

### Upstream Sync Issues
- Check if upstream remote is accessible
- Review merge conflicts manually if automatic merge fails
- Security-critical file conflicts require manual review

### Build Failures in Security Workflows
- Linting workflows are set to `continue-on-error: true`
- They won't block builds but provide valuable feedback
- Review and fix linting issues for better code quality

---

## 🔗 Additional Resources

- [GitHub Security Documentation](https://docs.github.com/en/code-security)
- [SonarCloud Documentation](https://docs.sonarcloud.io/)  
- [CodeQL Documentation](https://codeql.github.com/docs/)
- [Dependabot Documentation](https://docs.github.com/en/code-security/dependabot)

For questions about this security setup, please check the repository's `SECURITY.md` file or open an issue with the `security` label.